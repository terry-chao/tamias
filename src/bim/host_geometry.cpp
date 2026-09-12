#include "bim/host_geometry.h"

#include "engine/modeling/feature.h"
#include "entity/opening_entity.h"

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr double kPi = 3.14159265358979323846;

const Feature* first_of_kind(const FeatureModel& model, FeatureKind kind) {
  for (const auto& f : model.features()) {
    if (f.kind == kind) {
      return &f;
    }
  }
  return nullptr;
}

Feature* first_of_kind_mut(FeatureModel& model, FeatureKind kind) {
  for (auto& f : model.features()) {
    if (f.kind == kind) {
      return &f;
    }
  }
  return nullptr;
}

}  // namespace

WallSize wall_size(const Entity& wall) {
  WallSize size{};
  if (const Feature* profile = first_of_kind(wall.model, FeatureKind::RectProfile)) {
    size.thickness = wall.model.param(profile->id, "width", size.thickness);
    size.length = wall.model.param(profile->id, "height", size.length);
  }
  if (const Feature* extrude = first_of_kind(wall.model, FeatureKind::Extrude)) {
    size.height = wall.model.param(extrude->id, "depth", size.height);
  }
  size.thickness = std::max(size.thickness, 1e-3);
  size.length = std::max(size.length, 1e-3);
  size.height = std::max(size.height, 1e-3);
  return size;
}

OpeningSize opening_size(const Entity& opening) {
  OpeningSize size{};
  if (const Feature* profile = first_of_kind(opening.model, FeatureKind::RectProfile)) {
    size.width = opening.model.param(profile->id, "width", size.width);
    size.thickness = opening.model.param(profile->id, "height", size.thickness);
  }
  if (const Feature* extrude = first_of_kind(opening.model, FeatureKind::Extrude)) {
    size.height = opening.model.param(extrude->id, "depth", size.height);
  }
  size.width = std::max(size.width, 1e-3);
  size.height = std::max(size.height, 1e-3);
  size.thickness = std::max(size.thickness, 1e-3);
  return size;
}

void set_opening_thickness(Entity& opening, double thickness) {
  if (Feature* profile = first_of_kind_mut(opening.model, FeatureKind::RectProfile)) {
    opening.model.set_param(profile->id, "height", std::max(thickness, 1e-3));
  }
}

bool is_wall_host(const Entity& host) {
  return host.kind() == EntityKind::Wall || host.kind() == EntityKind::StructuralWall;
}

bool can_host_opening(const Entity& host, const Entity& guest) {
  if (!is_wall_host(host)) {
    return false;
  }
  return is_opening_entity(guest);
}

HostPlacement placement_from_world(const Entity& wall, const OpeningSize& /*opening*/, Vec3 world_point,
                                   double sill_height) {
  const WallSize size = wall_size(wall);
  const Vec3 local = invert_affine(wall.local_transform) * world_point;
  HostPlacement placement{};
  placement.along = static_cast<double>(local.z) / size.length + 0.5;
  placement.sill = sill_height;
  placement.offset = static_cast<double>(local.x);
  placement.handle_side = placement.offset >= 0.0 ? 1.0 : -1.0;
  return placement;
}

HostPlacement placement_from_world(const Entity& wall, const Entity& guest, Vec3 world_point) {
  return placement_from_world(wall, opening_size(guest), world_point,
                              opening_sill_height(guest));
}

std::vector<Vec3> opening_preview_polyline(const Entity& wall, const OpeningSize& opening,
                                           Vec3 world_point, double sill_height) {
  const WallSize size = wall_size(wall);
  HostPlacement placement = placement_from_world(wall, opening, world_point, sill_height);
  align_placement(placement, size, opening);

  const Vec3 hit_local = invert_affine(wall.local_transform) * world_point;
  const float half_t = static_cast<float>(size.thickness * 0.5);
  const float x0 = hit_local.x >= 0.f ? half_t : -half_t;
  const float x1 = -x0;
  const double zc = (placement.along - 0.5) * size.length;
  const double hw = opening.width * 0.5;
  const double y0 = placement.sill;
  const double y1 = placement.sill + opening.height;

  auto xf = [&](float x, double y, double z) {
    return wall.local_transform * Vec3{x, static_cast<float>(y), static_cast<float>(z)};
  };
  const Vec3 f0 = xf(x0, y0, zc - hw);
  const Vec3 f1 = xf(x0, y0, zc + hw);
  const Vec3 f2 = xf(x0, y1, zc + hw);
  const Vec3 f3 = xf(x0, y1, zc - hw);
  const Vec3 b0 = xf(x1, y0, zc - hw);
  const Vec3 b1 = xf(x1, y0, zc + hw);
  const Vec3 b2 = xf(x1, y1, zc + hw);
  const Vec3 b3 = xf(x1, y1, zc - hw);
  // 近面闭合矩形 + 一条穿墙边 + 远面闭合矩形。
  return {f0, f1, f2, f3, f0, b0, b3, b2, b1, b0};
}

void align_placement(HostPlacement& placement, const WallSize& wall, const OpeningSize& opening) {
  const double half_w = opening.width * 0.5;
  if (opening.width >= wall.length) {
    placement.along = 0.5;
  } else {
    const double min_along = half_w / wall.length;
    const double max_along = 1.0 - half_w / wall.length;
    placement.along = std::clamp(placement.along, min_along, max_along);
  }
  if (opening.height >= wall.height) {
    placement.sill = 0.0;
  } else {
    placement.sill = std::clamp(placement.sill, 0.0, wall.height - opening.height);
  }
  const double half_t = wall.thickness * 0.5;
  placement.offset = std::clamp(placement.offset, -half_t, half_t);
}

bool placement_is_valid(const HostPlacement& placement, const WallSize& wall,
                        const OpeningSize& opening) {
  if (opening.width > wall.length + 1e-6 || opening.height > wall.height + 1e-6) {
    return false;
  }
  const double along_m = placement.along * wall.length;
  if (along_m - opening.width * 0.5 < -1e-6 || along_m + opening.width * 0.5 > wall.length + 1e-6) {
    return false;
  }
  if (placement.sill < -1e-6 || placement.sill + opening.height > wall.height + 1e-6) {
    return false;
  }
  const double half_t = wall.thickness * 0.5;
  if (std::fabs(placement.offset) > half_t + 1e-6) {
    return false;
  }
  return true;
}

Mat4 hosted_transform(const Entity& wall, const HostPlacement& placement) {
  const WallSize size = wall_size(wall);
  const Vec3 local{static_cast<float>(placement.offset), static_cast<float>(placement.sill),
                   static_cast<float>((placement.along - 0.5) * size.length)};
  // 窗/门局部 X = 宽，墙局部 Z = 长；绕 Y 转 −90° 对齐。
  return wall.local_transform * translate(local) * rotate_y(static_cast<float>(-kPi * 0.5));
}

}  // namespace tamias
