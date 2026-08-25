#include "entity/entity_grip.h"

#include "bim/line_location.h"
#include "entity/entity.h"
#include "engine/modeling/curve_geom.h"
#include "engine/modeling/feature.h"

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr float kMinSize = 1e-3f;

Feature* find_kind(FeatureModel& model, FeatureKind kind) {
  for (Feature& f : model.features()) {
    if (f.kind == kind) {
      return &f;
    }
  }
  return nullptr;
}

const Feature* find_kind(const FeatureModel& model, FeatureKind kind) {
  for (const Feature& f : model.features()) {
    if (f.kind == kind) {
      return &f;
    }
  }
  return nullptr;
}

void push_world(std::vector<EntityGrip>& grips, std::uint64_t id, const Mat4& xf, Vec3 local) {
  EntityGrip g;
  g.entity_id = id;
  g.index = static_cast<int>(grips.size());
  g.world = xf * local;
  grips.push_back(g);
}

std::vector<Vec3> sketch_locals(const Entity& entity) {
  const Feature* out = entity.model.output_feature();
  if (out == nullptr) {
    return {};
  }
  switch (out->kind) {
    case FeatureKind::Line:
      return {feature_xyz(entity.model, out->id, "a"), feature_xyz(entity.model, out->id, "b")};
    case FeatureKind::Polyline:
      return polyline_points(entity.model, *out);
    case FeatureKind::Bezier:
      return bezier_control_points(entity.model, *out);
    case FeatureKind::BSpline:
    case FeatureKind::Nurbs:
      return spline_control_points(entity.model, *out);
    case FeatureKind::Arc:
      return {feature_xyz(entity.model, out->id, "a"), feature_xyz(entity.model, out->id, "b"),
              feature_xyz(entity.model, out->id, "c")};
    case FeatureKind::CircleWire: {
      const Vec3 c = feature_xyz(entity.model, out->id, "c");
      const float r = static_cast<float>(entity.model.param(out->id, "radius", 0.5));
      return {c, {c.x + r, c.y, c.z}};
    }
    case FeatureKind::RectWire:
      return rect_wire_points(entity.model, *out);
    default:
      return {};
  }
}

bool apply_sketch(Entity& entity, int index, Vec3 world) {
  const Feature* cout = entity.model.output_feature();
  if (cout == nullptr) {
    return false;
  }
  Feature* out = entity.model.find(cout->id);
  if (out == nullptr) {
    return false;
  }
  const Vec3 local = invert_affine(entity.local_transform) * world;
  switch (out->kind) {
    case FeatureKind::Line: {
      if (index == 0) {
        set_feature_xyz(entity.model, out->id, "a", local);
      } else if (index == 1) {
        set_feature_xyz(entity.model, out->id, "b", local);
      } else {
        return false;
      }
      return true;
    }
    case FeatureKind::Polyline: {
      const int n = static_cast<int>(entity.model.param(out->id, "n", 0.0));
      if (index < 0 || index >= n) {
        return false;
      }
      set_feature_xyz(entity.model, out->id, "p" + std::to_string(index), local);
      return true;
    }
    case FeatureKind::Bezier: {
      auto ctrls = bezier_control_points(entity.model, *out);
      if (index < 0 || index >= static_cast<int>(ctrls.size())) {
        return false;
      }
      ctrls[static_cast<std::size_t>(index)] = local;
      out->params = bezier_feature_params(ctrls);
      return true;
    }
    case FeatureKind::BSpline: {
      auto ctrls = spline_control_points(entity.model, *out);
      if (index < 0 || index >= static_cast<int>(ctrls.size())) {
        return false;
      }
      ctrls[static_cast<std::size_t>(index)] = local;
      out->params = bspline_feature_params(ctrls, spline_degree(entity.model, *out));
      return true;
    }
    case FeatureKind::Nurbs: {
      auto ctrls = spline_control_points(entity.model, *out);
      if (index < 0 || index >= static_cast<int>(ctrls.size())) {
        return false;
      }
      auto weights = nurbs_weights(entity.model, *out);
      ctrls[static_cast<std::size_t>(index)] = local;
      out->params = nurbs_feature_params(ctrls, weights, spline_degree(entity.model, *out));
      return true;
    }
    case FeatureKind::Arc: {
      if (index == 0) {
        set_feature_xyz(entity.model, out->id, "a", local);
      } else if (index == 1) {
        set_feature_xyz(entity.model, out->id, "b", local);
      } else if (index == 2) {
        set_feature_xyz(entity.model, out->id, "c", local);
      } else {
        return false;
      }
      return true;
    }
    case FeatureKind::CircleWire: {
      Vec3 c = feature_xyz(entity.model, out->id, "c");
      if (index == 0) {
        set_feature_xyz(entity.model, out->id, "c", local);
      } else if (index == 1) {
        const float r = std::max(kMinSize, std::sqrt((local.x - c.x) * (local.x - c.x) +
                                                     (local.z - c.z) * (local.z - c.z)));
        entity.model.set_param(out->id, "radius", static_cast<double>(r));
      } else {
        return false;
      }
      return true;
    }
    case FeatureKind::RectWire: {
      auto corners = rect_wire_points(entity.model, *out);
      if (index < 0 || index >= static_cast<int>(corners.size())) {
        return false;
      }
      const float y = corners[static_cast<std::size_t>(index)].y;
      corners[static_cast<std::size_t>(index)] = {local.x, y, local.z};
      out->params = polyline_feature_params(corners);
      float min_x = corners[0].x;
      float max_x = corners[0].x;
      float min_z = corners[0].z;
      float max_z = corners[0].z;
      for (const Vec3& p : corners) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_z = std::min(min_z, p.z);
        max_z = std::max(max_z, p.z);
      }
      set_feature_xyz(entity.model, out->id, "a", {min_x, y, min_z});
      set_feature_xyz(entity.model, out->id, "b", {max_x, y, max_z});
      return true;
    }
    default:
      return false;
  }
}

bool apply_segment(Entity& entity, int index, Vec3 world) {
  Feature* profile = find_kind(entity.model, FeatureKind::RectProfile);
  if (profile == nullptr || (index != 0 && index != 1)) {
    return false;
  }
  const float length = static_cast<float>(
      std::max(entity.model.param(profile->id, "height", 1.0), static_cast<double>(kMinSize)));
  const Vec3 start = entity.local_transform * Vec3{0.f, 0.f, -length * 0.5f};
  const Vec3 end = entity.local_transform * Vec3{0.f, 0.f, length * 0.5f};
  Vec3 a = index == 0 ? world : start;
  Vec3 b = index == 1 ? world : end;
  a.y = start.y;
  b.y = end.y;
  const Vec3 d = b - a;
  const float len = std::max(std::sqrt(d.x * d.x + d.z * d.z), kMinSize);
  const Vec3 mid{(a.x + b.x) * 0.5f, start.y, (a.z + b.z) * 0.5f};
  const float yaw = std::atan2(d.x, d.z);
  entity.model.set_param(profile->id, "height", static_cast<double>(len));
  const double storey_elevation =
      entity.location ? static_cast<double>(entity.local_transform(1, 3)) -
                            entity.location->elevation_offset()
                      : 0.0;
  if (entity.location && entity.location->kind() == LocationKind::Line) {
    auto* line = static_cast<LineLocation*>(entity.location.get());
    line->set_start(a);
    line->set_end(b);
    entity.sync_from_location(storey_elevation);
  } else {
    entity.local_transform = translate(mid) * rotate_y(yaw);
  }
  return true;
}

Feature* find_footprint(FeatureModel& model) {
  if (Feature* poly = find_kind(model, FeatureKind::PolygonProfile)) {
    return poly;
  }
  return find_kind(model, FeatureKind::RectProfile);
}

const Feature* find_footprint(const FeatureModel& model) {
  if (const Feature* poly = find_kind(model, FeatureKind::PolygonProfile)) {
    return poly;
  }
  return find_kind(model, FeatureKind::RectProfile);
}

std::vector<Vec3> footprint_corners(const FeatureModel& model, const Feature& profile) {
  if (profile.kind == FeatureKind::PolygonProfile) {
    return polyline_points(model, profile);
  }
  const float hw =
      static_cast<float>(std::max(model.param(profile.id, "width", 1.0), 0.0)) * 0.5f;
  const float hh =
      static_cast<float>(std::max(model.param(profile.id, "height", 1.0), 0.0)) * 0.5f;
  return {{-hw, 0.f, -hh}, {hw, 0.f, -hh}, {hw, 0.f, hh}, {-hw, 0.f, hh}};
}

void write_footprint_polygon(Feature& profile, const std::vector<Vec3>& corners) {
  auto params = polyline_feature_params(corners);
  if (!corners.empty()) {
    float min_x = corners[0].x;
    float max_x = corners[0].x;
    float min_z = corners[0].z;
    float max_z = corners[0].z;
    for (const Vec3& p : corners) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_z = std::min(min_z, p.z);
      max_z = std::max(max_z, p.z);
    }
    params["width"] = static_cast<double>(std::max(max_x - min_x, kMinSize));
    params["height"] = static_cast<double>(std::max(max_z - min_z, kMinSize));
  }
  profile.kind = FeatureKind::PolygonProfile;
  profile.params = std::move(params);
}

bool apply_footprint_corner(Entity& entity, int index, Vec3 world) {
  Feature* profile = find_footprint(entity.model);
  if (profile == nullptr || index < 0) {
    return false;
  }
  auto corners = footprint_corners(entity.model, *profile);
  if (index >= static_cast<int>(corners.size())) {
    return false;
  }
  Vec3 local = invert_affine(entity.local_transform) * world;
  local.y = 0.f;
  corners[static_cast<std::size_t>(index)] = local;
  write_footprint_polygon(*profile, corners);
  return true;
}

std::vector<Vec3> inferred_grip_locals(const Entity& entity) {
  switch (entity.kind()) {
    case EntityKind::Line:
    case EntityKind::Polyline:
    case EntityKind::Bezier:
    case EntityKind::BSpline:
    case EntityKind::Nurbs:
    case EntityKind::Arc:
    case EntityKind::Circle:
    case EntityKind::Rectangle:
      return sketch_locals(entity);
    case EntityKind::Wall:
    case EntityKind::Beam: {
      const Feature* profile = find_kind(entity.model, FeatureKind::RectProfile);
      if (profile == nullptr) {
        return {};
      }
      const float length = static_cast<float>(entity.model.param(profile->id, "height", 1.0));
      return {{0.f, 0.f, -length * 0.5f}, {0.f, 0.f, length * 0.5f}};
    }
    case EntityKind::Box:
    case EntityKind::Slab:
    case EntityKind::Column: {
      const Feature* profile = find_footprint(entity.model);
      if (profile == nullptr) {
        return {};
      }
      return footprint_corners(entity.model, *profile);
    }
    case EntityKind::Cylinder: {
      const Feature* profile = find_kind(entity.model, FeatureKind::CircleProfile);
      if (profile == nullptr) {
        return {};
      }
      const float r = static_cast<float>(entity.model.param(profile->id, "radius", 0.5));
      return {{}, {r, 0.f, 0.f}};
    }
    default:
      return {};
  }
}

}  // namespace

void sync_entity_grips(Entity& entity) { entity.grips = inferred_grip_locals(entity); }

std::vector<EntityGrip> collect_entity_grips(const Entity& entity) {
  std::vector<EntityGrip> grips;
  const Mat4& xf = entity.local_transform;
  const std::vector<Vec3> locals =
      entity.grips.empty() ? inferred_grip_locals(entity) : entity.grips;
  for (const Vec3& p : locals) {
    push_world(grips, entity.id, xf, p);
  }
  return grips;
}

bool apply_entity_grip(Entity& entity, int index, Vec3 world) {
  bool ok = false;
  switch (entity.kind()) {
    case EntityKind::Line:
    case EntityKind::Polyline:
    case EntityKind::Bezier:
    case EntityKind::BSpline:
    case EntityKind::Nurbs:
    case EntityKind::Arc:
    case EntityKind::Circle:
    case EntityKind::Rectangle:
      ok = apply_sketch(entity, index, world);
      break;
    case EntityKind::Wall:
    case EntityKind::Beam:
      ok = apply_segment(entity, index, world);
      break;
    case EntityKind::Box:
    case EntityKind::Slab:
    case EntityKind::Column:
      ok = apply_footprint_corner(entity, index, world);
      break;
    case EntityKind::Cylinder: {
      Feature* profile = find_kind(entity.model, FeatureKind::CircleProfile);
      if (profile == nullptr) {
        break;
      }
      const Vec3 t{entity.local_transform(0, 3), entity.local_transform(1, 3),
                   entity.local_transform(2, 3)};
      if (index == 0) {
        entity.local_transform = translate({world.x, t.y, world.z});
        ok = true;
      } else if (index == 1) {
        const float dx = world.x - t.x;
        const float dz = world.z - t.z;
        const float r = std::max(kMinSize, std::sqrt(dx * dx + dz * dz));
        entity.model.set_param(profile->id, "radius", static_cast<double>(r));
        ok = true;
      }
      break;
    }
    default:
      break;
  }
  if (ok) {
    sync_entity_grips(entity);
  }
  return ok;
}

}  // namespace tamias
