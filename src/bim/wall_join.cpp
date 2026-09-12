#include "bim/wall_join.h"

#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/line_location.h"
#include "engine/document/document.h"
#include "engine/modeling/curve_geom.h"
#include "engine/modeling/occt_geom_builder.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace tamias {
namespace {

// 端点重合 / 端点落在墙上 / 同标高，都按毫米级判定。
constexpr double kJunctionTolerance = 1e-3;
// 两墙斜接面互相咬入的余量：各多伸一点，交界面埋进对方体内，避免共面闪烁。
constexpr double kJunctionEmbed = 0.001;
// 两墙方向几乎相反（折回）时不斜接。
constexpr double kMinTurnSin = 0.05;
// 单端最大延长；超过说明交角极锐，退回硬拼，免得拉出一根长尖角。
constexpr double kMaxExtension = 2.0;

// 墙的放置框架（世界 XZ + 标高）。局部 +X = 墙厚方向，局部 +Z = 起点→终点。
struct WallFrame {
  Vec3 start{};
  Vec3 end{};
  Vec3 mid{};
  Vec3 axis{};
  Vec3 side{};
  double length = 0.0;
  double elevation = 0.0;
};

bool wall_frame_of(const Entity& wall, WallFrame& out) {
  if (!is_wall_host(wall) || wall.location == nullptr ||
      wall.location->kind() != LocationKind::Line) {
    return false;
  }
  const auto* line = static_cast<const LineLocation*>(wall.location.get());
  const Vec3 start = line->start();
  const Vec3 end = line->end();
  const double dx = static_cast<double>(end.x) - static_cast<double>(start.x);
  const double dz = static_cast<double>(end.z) - static_cast<double>(start.z);
  const double length = std::sqrt(dx * dx + dz * dz);
  if (length < 1e-4) {
    return false;
  }
  out.start = {start.x, 0.f, start.z};
  out.end = {end.x, 0.f, end.z};
  out.mid = (out.start + out.end) * 0.5f;
  out.axis = {static_cast<float>(dx / length), 0.f, static_cast<float>(dz / length)};
  out.side = {static_cast<float>(dz / length), 0.f, static_cast<float>(-dx / length)};
  out.length = length;
  // 不同标高（不同楼层）的墙不互相斜接。
  out.elevation = static_cast<double>(wall.local_transform(1, 3));
  return true;
}

float distance_xz(Vec3 a, Vec3 b) {
  const float dx = a.x - b.x;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dz * dz);
}

// 世界点 / 世界方向 → 墙局部 XZ。
Vec2 to_local(const WallFrame& frame, Vec3 world) {
  const Vec3 d = world - frame.mid;
  return {dot(d, frame.side), dot(d, frame.axis)};
}

Vec2 direction_to_local(const WallFrame& frame, Vec3 world_direction) {
  return {dot(world_direction, frame.side), dot(world_direction, frame.axis)};
}

// 本墙端点与邻墙的关系。
enum class TouchKind {
  None = 0,
  Corner,  // 端点与邻墙端点重合：L 角
  OnBody,  // 端点落在邻墙身上：T 形
};

TouchKind touch_kind(const WallFrame& self, const WallFrame& other, double other_thickness,
                     bool at_start, Vec2& on_other) {
  if (std::fabs(self.elevation - other.elevation) > kJunctionTolerance) {
    return TouchKind::None;
  }
  const Vec3 endpoint = at_start ? self.start : self.end;
  if (distance_xz(endpoint, other.start) <= kJunctionTolerance ||
      distance_xz(endpoint, other.end) <= kJunctionTolerance) {
    return TouchKind::Corner;
  }
  on_other = to_local(other, endpoint);
  // 落在邻墙身上 = 在墙厚范围内、且在墙长范围内（两端已按 L 角处理）。
  if (std::fabs(on_other.x) > other_thickness * 0.5 + kJunctionTolerance) {
    return TouchKind::None;
  }
  if (std::fabs(on_other.y) > other.length * 0.5 + kJunctionTolerance) {
    return TouchKind::None;
  }
  return TouchKind::OnBody;
}

// 端点相接时给出斜接面（世界坐标）：法线指向被裁掉的一侧，过点在面上。
bool junction_plane(const WallFrame& self, const WallFrame& other, double other_thickness,
                    bool at_start, Vec3& normal, Vec3& point) {
  Vec2 on_other{};
  const TouchKind kind = touch_kind(self, other, other_thickness, at_start, on_other);
  if (kind == TouchKind::None) {
    return false;
  }
  const Vec3 endpoint = at_start ? self.start : self.end;
  const Vec3 inward = at_start ? self.axis * -1.f : self.axis;  // 指向本墙端点

  if (kind == TouchKind::Corner) {
    // 斜接面平分两墙：法线 = 本墙朝向接点的方向 + 邻墙背离接点的方向。
    const bool at_other_start = distance_xz(endpoint, other.start) <= kJunctionTolerance;
    const Vec3 leaving = at_other_start ? other.axis : other.axis * -1.f;
    const Vec3 sum = inward + leaving;
    const float sum_length = length(sum);
    if (sum_length < kMinTurnSin) {
      return false;  // 两墙几乎折回，没有稳定的斜接面
    }
    normal = sum * (1.f / sum_length);
    point = endpoint;
    return true;
  }

  // T 形：本墙顶在邻墙侧面上，裁到邻墙近侧墙面。
  const float side_dot = dot(inward * -1.f, other.side);  // 本墙体在邻墙哪一侧
  if (std::fabs(side_dot) < 0.1f) {
    return false;  // 与邻墙几乎共线，无需裁
  }
  const float sign = side_dot > 0.f ? 1.f : -1.f;
  const Vec3 on_center = endpoint - other.side * on_other.x;
  normal = other.side * -sign;  // 从邻墙近侧墙面指向邻墙体内
  point = on_center + other.side * (sign * static_cast<float>(other_thickness * 0.5));
  return true;
}

// 该端为了够到斜接面需要延长的长度：取两条长边（x = ±半厚）上斜接面的 z。
double extension_needed(const WallFrame& frame, const WallJoint& joint, double half_thickness,
                        bool at_start) {
  const double nz = joint.normal_local.y;
  if (std::fabs(nz) < 1e-6) {
    return kMaxExtension;  // 斜接面平行于墙轴，长度会拉爆，判定为不可斜接
  }
  const double end_z = at_start ? -frame.length * 0.5 : frame.length * 0.5;
  const double toward_end = at_start ? -1.0 : 1.0;
  double needed = 0.0;
  for (const double x : {-half_thickness, half_thickness}) {
    const double z =
        joint.point_local.y - (joint.normal_local.x / nz) * (x - joint.point_local.x);
    needed = std::max(needed, toward_end * (z - end_z));
  }
  return std::max(needed, 0.0) + kJunctionEmbed;
}

void collect_junctions(const WallFrame& self, double self_thickness, const WallFrame& other,
                       double other_thickness, std::uint64_t other_id,
                       std::vector<WallJoint>& out) {
  const double half_thickness = self_thickness * 0.5;
  for (int end = 0; end < 2; ++end) {
    const bool at_start = end == 0;
    Vec3 normal{};
    Vec3 point{};
    if (!junction_plane(self, other, other_thickness, at_start, normal, point)) {
      continue;
    }
    WallJoint joint;
    joint.neighbor_id = other_id;
    joint.at_start = at_start;
    joint.normal_local = direction_to_local(self, normal);
    joint.point_local = to_local(self, point);
    joint.extension = extension_needed(self, joint, half_thickness, at_start);
    if (joint.extension > kMaxExtension) {
      continue;
    }
    out.push_back(joint);
  }
}

// 半空间裁剪（Sutherland–Hodgman）：保留 n·(p - point) <= embed 的一侧。
std::vector<Vec2> clip_half_plane(const std::vector<Vec2>& polygon, Vec2 normal, Vec2 point) {
  std::vector<Vec2> out;
  const std::size_t count = polygon.size();
  if (count == 0) {
    return out;
  }
  out.reserve(count + 1);
  for (std::size_t i = 0; i < count; ++i) {
    const Vec2 a = polygon[i];
    const Vec2 b = polygon[(i + 1) % count];
    const double da = normal.x * (a.x - point.x) + normal.y * (a.y - point.y) - kJunctionEmbed;
    const double db = normal.x * (b.x - point.x) + normal.y * (b.y - point.y) - kJunctionEmbed;
    const bool inside_a = da <= 0.0;
    const bool inside_b = db <= 0.0;
    if (inside_a) {
      out.push_back(a);
    }
    if (inside_a != inside_b) {
      const double t = da / (da - db);
      out.push_back({static_cast<float>(a.x + (b.x - a.x) * t),
                     static_cast<float>(a.y + (b.y - a.y) * t)});
    }
  }
  return out;
}

// 基础轮廓（第一个矩形轮廓）与吃掉它的那次拉伸。
bool base_profile_and_extrude(const FeatureModel& model, const Feature*& profile,
                              const Feature*& extrude) {
  profile = nullptr;
  extrude = nullptr;
  for (const Feature& f : model.features()) {
    if (profile == nullptr) {
      if (f.kind == FeatureKind::RectProfile) {
        profile = &f;
      }
      continue;
    }
    if (f.kind == FeatureKind::Extrude && !f.inputs.empty() &&
        f.inputs.front() == profile->id) {
      extrude = &f;
      return true;
    }
  }
  return false;
}

}  // namespace

std::vector<WallJoint> find_wall_junctions(const Entity& wall, const Document& document) {
  std::vector<WallJoint> joints;
  WallFrame self{};
  if (!wall_frame_of(wall, self)) {
    return joints;
  }
  const double self_thickness = wall_size(wall).thickness;
  for (const auto& [id, other] : document.entities()) {
    if (other == nullptr || id == wall.id) {
      continue;
    }
    WallFrame frame{};
    if (!wall_frame_of(*other, frame)) {
      continue;
    }
    collect_junctions(self, self_thickness, frame, wall_size(*other).thickness, id, joints);
  }
  return joints;
}

std::vector<Vec3> wall_junction_footprint(const WallSize& size,
                                          const std::vector<WallJoint>& joints) {
  const double half_thickness = std::max(size.thickness, 1e-3) * 0.5;
  const double half_length = std::max(size.length, 1e-3) * 0.5;
  double extend_start = 0.0;
  double extend_end = 0.0;
  for (const WallJoint& joint : joints) {
    if (joint.at_start) {
      extend_start = std::max(extend_start, joint.extension);
    } else {
      extend_end = std::max(extend_end, joint.extension);
    }
  }
  const float start_z = static_cast<float>(-half_length - extend_start);
  const float end_z = static_cast<float>(half_length + extend_end);
  const float hw = static_cast<float>(half_thickness);
  std::vector<Vec2> polygon = {{-hw, start_z}, {hw, start_z}, {hw, end_z}, {-hw, end_z}};
  for (const WallJoint& joint : joints) {
    std::vector<Vec2> clipped = clip_half_plane(polygon, joint.normal_local, joint.point_local);
    if (clipped.size() >= 3) {
      polygon = std::move(clipped);
    }
  }
  std::vector<Vec3> points;
  points.reserve(polygon.size());
  for (const Vec2& p : polygon) {
    points.push_back({p.x, 0.f, p.y});
  }
  return points;
}

FeatureModel wall_junction_model(const Entity& wall, const std::vector<WallJoint>& joints) {
  if (joints.empty()) {
    return wall.model;
  }
  const WallSize size = wall_size(wall);
  const std::vector<Vec3> footprint = wall_junction_footprint(size, joints);
  if (footprint.size() < 3) {
    return wall.model;
  }
  const Feature* base_profile = nullptr;
  const Feature* base_extrude = nullptr;
  if (!base_profile_and_extrude(wall.model, base_profile, base_extrude)) {
    return wall.model;  // 形状不是「矩形轮廓 + 拉伸」，不接管
  }

  FeatureModel joined;
  auto params = polyline_feature_params(footprint);
  params["width"] = size.thickness;
  params["height"] = size.length;
  auto& profile = joined.add_feature(FeatureKind::PolygonProfile, {}, std::move(params));
  auto& extrude = joined.add_feature(
      FeatureKind::Extrude, {profile.id},
      {{"depth", wall.model.param(base_extrude->id, "depth", size.height)}});
  // 其余特征（空腔内箱、布尔、开口切减）按拓扑序抄过来，把对基础轮廓/拉伸的
  // 引用改指向新特征。
  std::unordered_map<std::uint64_t, std::uint64_t> remap{{base_profile->id, profile.id},
                                                         {base_extrude->id, extrude.id}};
  for (const Feature& f : wall.model.features()) {
    if (f.id == base_profile->id || f.id == base_extrude->id) {
      continue;
    }
    Feature copy = f;
    const std::uint64_t old_id = copy.id;
    for (std::uint64_t& input : copy.inputs) {
      const auto it = remap.find(input);
      if (it != remap.end()) {
        input = it->second;
      }
    }
    auto& added = joined.add_feature(copy.kind, copy.inputs, copy.params);
    remap[old_id] = added.id;
  }
  return joined;
}

FeatureModel wall_render_model(const Entity& wall, const Document& document) {
  if (!is_wall_host(wall)) {
    return wall.model;
  }
  FeatureModel model = wall_junction_model(wall, find_wall_junctions(wall, document));
  if (wall.id == 0) {
    return model;
  }
  const std::vector<const Relation*> openings = document.bim().dependents(wall.id);
  if (!openings.empty()) {
    if (const Feature* output = model.output_feature(); output != nullptr) {
      std::uint64_t current = output->id;
      append_hosted_opening_cuts(model, current, wall, openings, document);
    }
  }
  return model;
}

std::vector<std::uint64_t> wall_neighborhood(const Document& document, std::uint64_t wall_id) {
  std::vector<std::uint64_t> ids;
  const Entity* wall = document.entity(wall_id);
  WallFrame self{};
  if (wall == nullptr || !wall_frame_of(*wall, self)) {
    return ids;  // 不是墙（或退化）：没有交接可刷
  }
  const double self_thickness = wall_size(*wall).thickness;
  ids.push_back(wall_id);
  for (const auto& [id, other] : document.entities()) {
    if (other == nullptr || id == wall_id) {
      continue;
    }
    WallFrame frame{};
    if (!wall_frame_of(*other, frame)) {
      continue;
    }
    const double other_thickness = wall_size(*other).thickness;
    Vec2 unused{};
    const bool touches = touch_kind(self, frame, other_thickness, true, unused) !=
                             TouchKind::None ||
                         touch_kind(self, frame, other_thickness, false, unused) !=
                             TouchKind::None ||
                         touch_kind(frame, self, self_thickness, true, unused) !=
                             TouchKind::None ||
                         touch_kind(frame, self, self_thickness, false, unused) !=
                             TouchKind::None;
    if (touches) {
      ids.push_back(id);
    }
  }
  return ids;
}

Result<void> remesh_wall(Document& document, std::uint64_t wall_id) {
  Entity* wall = document.entity(wall_id);
  if (wall == nullptr || !is_wall_host(*wall)) {
    return {};
  }
  FeatureModel model = wall_render_model(*wall, document);
  auto mesh = geometry_builder().build(model, 0.05);
  if (!mesh) {
    return Err(mesh.error());
  }
  if (!document.replace_entity_mesh(wall_id, std::move(*mesh))) {
    return Err("wall_join: mesh asset not found");
  }
  document.scene().set_transform(wall_id, wall->local_transform);
  document.recompute_scene();
  document.mark_dirty();
  return {};
}

Result<void> remesh_wall_neighborhood(Document& document, std::uint64_t wall_id) {
  if (auto r = remesh_wall(document, wall_id); !r) {
    return r;
  }
  return remesh_wall_neighbors(document, wall_id);
}

Result<void> remesh_wall_neighbors(Document& document, std::uint64_t wall_id) {
  for (const std::uint64_t id : wall_neighborhood(document, wall_id)) {
    if (id == wall_id) {
      continue;
    }
    if (auto r = remesh_wall(document, id); !r) {
      return r;
    }
  }
  return {};
}

}  // namespace tamias
