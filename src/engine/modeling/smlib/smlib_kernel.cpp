#include "engine/modeling/smlib/smlib_kernel.h"

#include "engine/modeling/kernel/kernel.h"
#include "engine/modeling/smlib/smlib_c.h"

#include "engine/base/log.h"
#include "engine/graphics/mesh.h"

#include <cstdint>
#include <memory>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace tamias {
namespace {

std::string last_error(const char* fallback) {
  const char* message = smlib_c::last_error();
  if (message != nullptr && message[0] != '\0') {
    return std::string(message);
  }
  return std::string("smlib: ") + fallback + " failed";
}

[[nodiscard]] Vec3 to_vec3(const smlib_c::Vec3& v) {
  return Vec3{static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

class SmlibBody final : public Body {
 public:
  enum class Kind { Profile, Solid };

  explicit SmlibBody(smlib_c::Profile* profile) : kind_(Kind::Profile), profile_(profile) {}
  explicit SmlibBody(smlib_c::Brep* brep) : kind_(Kind::Solid), brep_(brep) {}
  ~SmlibBody() override {
    if (profile_ != nullptr) {
      smlib_c::free_profile(profile_);
    }
    if (brep_ != nullptr) {
      smlib_c::free_brep(brep_);
    }
  }

  SmlibBody(const SmlibBody&) = delete;
  SmlibBody& operator=(const SmlibBody&) = delete;

  [[nodiscard]] std::string backend_name() const override { return "smlib"; }
  [[nodiscard]] Kind kind() const { return kind_; }
  [[nodiscard]] const smlib_c::Profile* profile() const { return profile_; }
  [[nodiscard]] const smlib_c::Brep* brep() const { return brep_; }

 private:
  Kind kind_;
  smlib_c::Profile* profile_ = nullptr;
  smlib_c::Brep* brep_ = nullptr;
};

const SmlibBody* as_profile(const Body& body, const char* what) {
  const auto* smlib_body = dynamic_cast<const SmlibBody*>(&body);
  if (smlib_body == nullptr || smlib_body->kind() != SmlibBody::Kind::Profile) {
    return nullptr;
  }
  (void)what;
  return smlib_body;
}

const SmlibBody* as_solid(const Body& body) {
  const auto* smlib_body = dynamic_cast<const SmlibBody*>(&body);
  if (smlib_body == nullptr || smlib_body->kind() != SmlibBody::Kind::Solid) {
    return nullptr;
  }
  return smlib_body;
}

Result<BodyRef> wrap_profile(smlib_c::Profile* profile, const char* what) {
  if (profile == nullptr) {
    return Err(last_error(what));
  }
  return BodyRef{std::make_shared<SmlibBody>(profile)};
}

Result<BodyRef> wrap_solid(smlib_c::Brep* brep, const char* what) {
  if (brep == nullptr) {
    return Err(last_error(what));
  }
  return BodyRef{std::make_shared<SmlibBody>(brep)};
}

class SmlibKernel final : public ModelKernel {
 public:
  [[nodiscard]] KernelBackend backend() const override { return KernelBackend::Smlib; }

  [[nodiscard]] std::string version() const override { return "USD BRep 1.0.0 (SMLib)"; }

  [[nodiscard]] KernelCapabilities capabilities() const override {
    KernelCapabilities caps;
    caps.verbs = KernelVerb::RectFace | KernelVerb::CircleFace | KernelVerb::PolygonFace |
                 KernelVerb::Extrude | KernelVerb::Boolean | KernelVerb::Transform |
                 KernelVerb::Cylinder | KernelVerb::Edges | KernelVerb::MeasureEdges |
                 KernelVerb::Tessellate | KernelVerb::Bounds | KernelVerb::Fillet |
                 KernelVerb::Chamfer;
    caps.multi_edge_fillet = true;
    // 内核支持变半径，但 kernel.h 的 fillet 只有一个 radius 参数，别虚报。
    caps.variable_radius_fillet = false;
    // 导入导出都没有接：SM_API 只有原生 .smb，STEP/IGES 仍归 OCCT。
    caps.step_import = false;
    caps.step_export = false;
    caps.iges_import = false;
    caps.iges_export = false;
    caps.brep_import = false;
    caps.brep_export = false;
    return caps;
  }

  [[nodiscard]] Result<BodyRef> make_rect_face(double width, double height) const override {
    if (!(width > 0.0) || !(height > 0.0)) {
      return Err("make_rect_face: width and height must be positive");
    }
    smlib_c::Profile* profile = nullptr;
    if (smlib_c::make_rect_profile(width, height, &profile) != 0) {
      return Err(last_error("make_rect_face"));
    }
    return wrap_profile(profile, "make_rect_face");
  }

  [[nodiscard]] Result<BodyRef> make_circle_face(double radius) const override {
    if (!(radius > 0.0)) {
      return Err("make_circle_face: radius must be positive");
    }
    smlib_c::Profile* profile = nullptr;
    if (smlib_c::make_circle_profile(radius, &profile) != 0) {
      return Err(last_error("make_circle_face"));
    }
    return wrap_profile(profile, "make_circle_face");
  }

  [[nodiscard]] Result<BodyRef> make_polygon_face(std::span<const Vec3> loop) const override {
    if (loop.size() < 3) {
      return Err("make_polygon_face: needs at least 3 points");
    }
    std::vector<smlib_c::Vec3> points;
    points.reserve(loop.size());
    for (const Vec3& p : loop) {
      points.push_back(smlib_c::Vec3{p.x, p.y, p.z});
    }
    smlib_c::Profile* profile = nullptr;
    if (smlib_c::make_polygon_profile(points.data(), points.size(), &profile) != 0) {
      return Err(last_error("make_polygon_face"));
    }
    return wrap_profile(profile, "make_polygon_face");
  }

  [[nodiscard]] Result<BodyRef> extrude(const Body& profile, double depth) const override {
    const SmlibBody* body = as_profile(profile, "extrude");
    if (body == nullptr) {
      return Err("extrude: body is not an SMLib profile");
    }
    if (!(depth > 0.0)) {
      return Err("extrude: depth must be positive");
    }
    smlib_c::Brep* solid = nullptr;
    if (smlib_c::extrude(body->profile(), depth, &solid) != 0) {
      return Err(last_error("extrude"));
    }
    return wrap_solid(solid, "extrude");
  }

  [[nodiscard]] Result<BodyRef> boolean(const Body& a, const Body& b,
                                       BooleanOp op) const override {
    const SmlibBody* body_a = as_solid(a);
    const SmlibBody* body_b = as_solid(b);
    if (body_a == nullptr || body_b == nullptr) {
      return Err("boolean: bodies are not SMLib solids");
    }
    smlib_c::Brep* result = nullptr;
    if (smlib_c::boolean(body_a->brep(), body_b->brep(), static_cast<int>(op), &result) != 0) {
      return Err(last_error("boolean"));
    }
    return wrap_solid(result, "boolean");
  }

  [[nodiscard]] Result<BodyRef> transform(const Body& body, Vec3 translation) const override {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err("transform: body is not an SMLib solid");
    }
    smlib_c::Brep* moved = nullptr;
    const smlib_c::Vec3 offset{translation.x, translation.y, translation.z};
    if (smlib_c::transform(solid->brep(), offset, &moved) != 0) {
      return Err(last_error("transform"));
    }
    return wrap_solid(moved, "transform");
  }

  [[nodiscard]] Result<BodyRef> cylinder(double radius, double height, Vec3 center,
                                        Vec3 axis) const override {
    smlib_c::Brep* brep = nullptr;
    const smlib_c::Vec3 origin{center.x, center.y, center.z};
    const smlib_c::Vec3 direction{axis.x, axis.y, axis.z};
    if (smlib_c::cylinder(radius, height, origin, direction, &brep) != 0) {
      return Err(last_error("cylinder"));
    }
    return wrap_solid(brep, "cylinder");
  }

  [[nodiscard]] Result<std::vector<EdgeId>> edges(const Body& body) const override {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err("edges: body is not an SMLib solid");
    }
    std::size_t count = 0;
    if (smlib_c::edge_count(solid->brep(), &count) != 0) {
      return Err(last_error("edges"));
    }
    std::vector<EdgeId> ids(count);
    std::iota(ids.begin(), ids.end(), EdgeId{0});
    return ids;
  }

  [[nodiscard]] Result<std::vector<EdgeMeasure>> measure_edges(
      const Body& body) const override {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err("measure_edges: body is not an SMLib solid");
    }
    smlib_c::EdgeMeasure* raw = nullptr;
    std::size_t count = 0;
    if (smlib_c::measure_edges(solid->brep(), &raw, &count) != 0) {
      return Err(last_error("measure_edges"));
    }
    std::vector<EdgeMeasure> measures(count);
    for (std::size_t i = 0; i < count; ++i) {
      EdgeMeasure& out = measures[i];
      out.key = raw[i].key;
      out.mid = to_vec3(raw[i].mid);
      out.dir = to_vec3(raw[i].dir);
      out.has_dir = raw[i].has_dir;
      out.length = raw[i].length;
    }
    smlib_c::free_edge_measures(raw);
    return measures;
  }

  [[nodiscard]] Result<BodyRef> fillet(const Body& body, std::span<const EdgeId> edges,
                                       double radius) const override {
    return fillet_or_chamfer(body, edges, radius, true);
  }

  [[nodiscard]] Result<BodyRef> chamfer(const Body& body, std::span<const EdgeId> edges,
                                        double distance) const override {
    return fillet_or_chamfer(body, edges, distance, false);
  }

  [[nodiscard]] Result<MeshCpu> tessellate(const Body& body,
                                           double linear_deflection) const override {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err("tessellate: body is not an SMLib solid");
    }
    smlib_c::Mesh* mesh = nullptr;
    if (smlib_c::tessellate(solid->brep(), linear_deflection, &mesh) != 0) {
      return Err(last_error("tessellate"));
    }
    smlib_c::MeshView view{};
    if (smlib_c::mesh_view(mesh, &view) != 0) {
      smlib_c::free_mesh(mesh);
      return Err(last_error("tessellate"));
    }
    MeshCpu out;
    out.vertices.resize(view.vertex_count);
    for (std::size_t i = 0; i < view.vertex_count; ++i) {
      Vertex& vertex = out.vertices[i];
      vertex.position = Vec3{view.positions[i * 3 + 0], view.positions[i * 3 + 1],
                             view.positions[i * 3 + 2]};
      vertex.normal = Vec3{view.normals[i * 3 + 0], view.normals[i * 3 + 1],
                           view.normals[i * 3 + 2]};
      vertex.color = Vec3{1.f, 1.f, 1.f};
    }
    out.indices.resize(view.vertex_count);
    std::iota(out.indices.begin(), out.indices.end(), std::uint32_t{0});
    smlib_c::free_mesh(mesh);
    if (out.indices.empty()) {
      return Err("tessellate: SMLib produced an empty mesh");
    }
    recompute_bounds(out);
    return out;
  }

  [[nodiscard]] Result<Aabb> bounds(const Body& body) const override {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err("bounds: body is not an SMLib solid");
    }
    smlib_c::Bounds raw{};
    if (smlib_c::bounds(solid->brep(), &raw) != 0) {
      return Err(last_error("bounds"));
    }
    Aabb out{};
    out.expand(Vec3{static_cast<float>(raw.min[0]), static_cast<float>(raw.min[1]),
                    static_cast<float>(raw.min[2])});
    out.expand(Vec3{static_cast<float>(raw.max[0]), static_cast<float>(raw.max[1]),
                    static_cast<float>(raw.max[2])});
    return out;
  }

 private:
  [[nodiscard]] Result<BodyRef> fillet_or_chamfer(const Body& body,
                                                  std::span<const EdgeId> edges, double size,
                                                  bool is_fillet) const {
    const SmlibBody* solid = as_solid(body);
    if (solid == nullptr) {
      return Err(is_fillet ? "fillet: body is not an SMLib solid"
                           : "chamfer: body is not an SMLib solid");
    }
    if (edges.empty()) {
      return Err(is_fillet ? "fillet: no edges" : "chamfer: no edges");
    }
    smlib_c::Brep* result = nullptr;
    const int rc = is_fillet
                       ? smlib_c::fillet(solid->brep(), edges.data(), edges.size(), size, &result)
                       : smlib_c::chamfer(solid->brep(), edges.data(), edges.size(), size, &result);
    if (rc != 0) {
      return Err(last_error(is_fillet ? "fillet" : "chamfer"));
    }
    return wrap_solid(result, is_fillet ? "fillet" : "chamfer");
  }
};

}  // namespace

void register_smlib_kernel_backend() {
  register_kernel_backend(KernelModule{
      KernelBackend::Smlib,
      [](const KernelCreateInfo&) -> Result<std::unique_ptr<ModelKernel>> {
        if (smlib_c::init() != 0) {
          return Err(last_error("init"));
        }
        log_info("SMLib kernel backend initialised (USD BRep 1.0.0)");
        return std::make_unique<SmlibKernel>();
      }});
}

}  // namespace tamias
