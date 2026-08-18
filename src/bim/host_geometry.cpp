#include "bim/host_geometry.h"

#include "engine/modeling/feature.h"

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

bool can_host_opening(const Entity& host, const Entity& guest) {
  if (host.kind() != EntityKind::Wall) {
    return false;
  }
  return guest.kind() == EntityKind::Window || guest.kind() == EntityKind::Door;
}

HostPlacement placement_from_world(const Entity& wall, const Entity& guest, Vec3 world_point) {
  const WallSize size = wall_size(wall);
  const OpeningSize opening = opening_size(guest);
  const Vec3 local = invert_affine(wall.local_transform) * world_point;
  HostPlacement placement{};
  placement.along = static_cast<double>(local.z) / size.length + 0.5;
  // 点击点当作开口中心，窗台 = 点击高度 − 半窗高。
  placement.sill = static_cast<double>(local.y) - opening.height * 0.5;
  placement.offset = static_cast<double>(local.x);
  if (guest.kind() == EntityKind::Door) {
    placement.sill = 0.0;
  }
  return placement;
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
