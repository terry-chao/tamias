#include "command/edit/entity_transform.h"

#include "bim/line_location.h"
#include "bim/point_location.h"
#include "bim/surface_location.h"
#include "engine/document/document.h"
#include "engine/modeling/feature/curve_geom.h"
#include "entity/core/entity.h"
#include "entity/core/entity_grip.h"

#include <cmath>

namespace tamias {
namespace {

constexpr double kPi = 3.14159265358979323846;

// 镜像平面的单位法线（XZ 内，与平面方向垂直）。
[[nodiscard]] Vec2 plane_normal_xz(Vec3 plane_direction) {
  const float len = std::sqrt(plane_direction.x * plane_direction.x +
                              plane_direction.z * plane_direction.z);
  if (len < 1e-6f) {
    return {0.f, 1.f};  // 没给方向：以 +X 为平面方向
  }
  // 平面方向 d 在 XZ 内，法线取 d 逆时针转 90°。
  return {plane_direction.z / len, -plane_direction.x / len};
}

[[nodiscard]] Vec3 reflect(Vec3 p, Vec3 plane_point, Vec2 normal) {
  // 平面过 plane_point 且含竖轴：投影到 XZ，y 保持不变。
  const float dx = p.x - plane_point.x;
  const float dz = p.z - plane_point.z;
  const float d = dx * normal.x + dz * normal.y;
  return {p.x - 2.f * d * normal.x, p.y, p.z - 2.f * d * normal.y};
}

Feature* output_feature(Entity& entity) {
  const Feature* out = entity.model.output_feature();
  return out != nullptr ? entity.model.find(out->id) : nullptr;
}

// 草图实体：控制点反射后写回特征树（局部=世界，local_transform 归位）。
Result<void> mirror_sketch(Entity& entity, Vec3 plane_point, Vec3 plane_direction) {
  Feature* out = output_feature(entity);
  if (out == nullptr) {
    return Err("mirror: sketch has no output feature");
  }
  const Vec2 normal = plane_normal_xz(plane_direction);
  const Mat4 xf = entity.local_transform;
  auto flip = [&](Vec3 local) { return reflect(xf * local, plane_point, normal); };
  FeatureModel& model = entity.model;
  const std::uint64_t id = out->id;

  switch (out->kind) {
    case FeatureKind::Line: {
      const Vec3 a = flip(feature_xyz(model, id, "a"));
      const Vec3 b = flip(feature_xyz(model, id, "b"));
      set_feature_xyz(model, id, "a", a);
      set_feature_xyz(model, id, "b", b);
      break;
    }
    case FeatureKind::Polyline: {
      std::vector<Vec3> points = polyline_points(model, *out);
      for (Vec3& p : points) {
        p = flip(p);
      }
      out->params = polyline_feature_params(points);
      break;
    }
    case FeatureKind::RectWire: {
      // 矩形反射后仍是矩形，但斜着的镜像轴会让它不再与坐标轴对齐；矩形的点参数
      // 本来就支持任意四点（n>=4 走折线），所以直接写四点，几何精确。
      std::vector<Vec3> corners = rect_wire_points(model, *out);
      for (Vec3& p : corners) {
        p = flip(p);
      }
      out->params = polyline_feature_params(corners);
      break;
    }
    case FeatureKind::CircleWire: {
      const Vec3 center = flip(feature_xyz(model, id, "c"));
      const double radius = model.param(id, "radius", 0.5);
      out->params = circle_wire_params(center, radius);
      break;
    }
    case FeatureKind::Arc: {
      const Vec3 a = flip(feature_xyz(model, id, "a"));
      const Vec3 b = flip(feature_xyz(model, id, "b"));
      const Vec3 c = flip(feature_xyz(model, id, "c"));
      out->params = arc_feature_params(a, b, c);
      break;
    }
    case FeatureKind::Bezier: {
      std::vector<Vec3> points = bezier_control_points(model, *out);
      for (Vec3& p : points) {
        p = flip(p);
      }
      out->params = bezier_feature_params(points);
      break;
    }
    case FeatureKind::BSpline: {
      const int degree = spline_degree(model, *out);
      std::vector<Vec3> points = spline_control_points(model, *out);
      for (Vec3& p : points) {
        p = flip(p);
      }
      out->params = bspline_feature_params(points, degree);
      break;
    }
    case FeatureKind::Nurbs: {
      const int degree = spline_degree(model, *out);
      const std::vector<float> weights = nurbs_weights(model, *out);
      std::vector<Vec3> points = spline_control_points(model, *out);
      for (Vec3& p : points) {
        p = flip(p);
      }
      out->params = nurbs_feature_params(points, weights, degree);
      break;
    }
    default:
      return Err("mirror: unsupported sketch feature");
  }
  entity.local_transform = Mat4::identity();
  sync_entity_grips(entity);
  return {};
}

// 基础体（盒 / 圆柱）：截面在 XZ 内对称，摆放取镜像后的位置与朝向即可。
void mirror_primitive(Entity& entity, Vec3 plane_point, Vec2 normal) {
  const Mat4& m = entity.local_transform;
  const Vec3 origin{m(0, 3), m(1, 3), m(2, 3)};
  // 局部 +Z 在世界的朝向（rotate_y 约定：+Z → (sin a, 0, cos a)）。
  const Vec3 axis{m(0, 2), 0.f, m(2, 2)};
  const Vec3 mirrored_axis = reflect(axis, Vec3{0.f, 0.f, 0.f}, normal);
  const float yaw = std::atan2(mirrored_axis.x, mirrored_axis.z);
  entity.local_transform = translate(reflect(origin, plane_point, normal)) * rotate_y(yaw);
}

}  // namespace

Mat4 translation_transform(Vec3 delta) { return translate(delta); }

Mat4 yaw_rotation_about(Vec3 base, double angle_rad) {
  return translate(base) * rotate_y(static_cast<float>(angle_rad)) * translate(base * -1.f);
}

Vec3 mirror_point(Vec3 point, Vec3 plane_point, Vec3 plane_direction) {
  return reflect(point, plane_point, plane_normal_xz(plane_direction));
}

Vec3 mirror_direction(Vec3 direction, Vec3 plane_direction) {
  return reflect(direction, Vec3{0.f, 0.f, 0.f}, plane_normal_xz(plane_direction));
}

Result<std::vector<Mat4>> linear_array_placements(Vec3 direction, double spacing, int count) {
  if (count < 2) {
    return Err("array: count must be at least 2");
  }
  const float len = length(direction);
  if (len < 1e-6f) {
    return Err("array: linear direction must not be zero");
  }
  if (!(spacing > 0.0)) {
    return Err("array: spacing must be positive");
  }
  const Vec3 unit = direction * (1.f / len);
  std::vector<Mat4> placements;
  placements.reserve(static_cast<std::size_t>(count - 1));
  for (int i = 1; i < count; ++i) {
    placements.push_back(translation_transform(unit * static_cast<float>(spacing * i)));
  }
  return placements;
}

Result<std::vector<Mat4>> polar_array_placements(Vec3 center, double step_angle_deg, int count) {
  if (count < 2) {
    return Err("array: count must be at least 2");
  }
  if (std::fabs(step_angle_deg) < 1e-9) {
    return Err("array: polar step angle must not be zero");
  }
  std::vector<Mat4> placements;
  placements.reserve(static_cast<std::size_t>(count - 1));
  for (int i = 1; i < count; ++i) {
    const double angle = step_angle_deg * kPi / 180.0 * static_cast<double>(i);
    placements.push_back(yaw_rotation_about(center, angle));
  }
  return placements;
}

Mat4 entity_world_transform(const Entity& entity) { return entity.local_transform; }

void apply_entity_placement(Document& document, Entity& entity, const Mat4& transform) {
  if (entity.location != nullptr) {
    const double storey_elevation =
        document.bim().storey_elevation(entity.location->storey_id());
    entity.sync_location_from_transform(transform, storey_elevation);
  } else {
    entity.local_transform = transform;
  }
  sync_entity_grips(entity);
}

void set_entity_world_transform(Document& document, Entity& entity, const Mat4& transform) {
  apply_entity_placement(document, entity, transform);
  document.scene().set_transform(entity.id, entity.local_transform);
}

Result<void> mirror_entity(Document& document, Entity& entity, Vec3 plane_point,
                           Vec3 plane_direction) {
  const Vec2 normal = plane_normal_xz(plane_direction);
  if (entity.location != nullptr) {
    // 反射定位锚点：Location::transform() 会据此重建一个右手系摆放，所以镜像不会
    // 变成反射矩阵。竖直位置存在 elevation_offset 里，镜像不动它。
    switch (entity.location->kind()) {
      case LocationKind::Point: {
        auto* point = static_cast<PointLocation*>(entity.location.get());
        point->set_point(reflect(point->point(), plane_point, normal));
        break;
      }
      case LocationKind::Line: {
        auto* line = static_cast<LineLocation*>(entity.location.get());
        const Vec3 start = reflect(line->start(), plane_point, normal);
        const Vec3 end = reflect(line->end(), plane_point, normal);
        line->set_start(start);
        line->set_end(end);
        break;
      }
      case LocationKind::Surface: {
        auto* surface = static_cast<SurfaceLocation*>(entity.location.get());
        surface->set_origin(reflect(surface->origin(), plane_point, normal));
        surface->set_x_axis(reflect(surface->x_axis(), Vec3{0.f, 0.f, 0.f}, normal));
        break;
      }
    }
    const double storey_elevation =
        document.bim().storey_elevation(entity.location->storey_id());
    entity.sync_from_location(storey_elevation);
  } else if (entity.is_sketch_entity()) {
    if (auto r = mirror_sketch(entity, plane_point, plane_direction); !r) {
      return r;
    }
  } else {
    // 门窗的摆放由宿主墙决定（走 relation），走到这里说明调用方漏了；剩下的是
    // 盒 / 圆柱这类截面在 XZ 内对称的基础体。
    mirror_primitive(entity, plane_point, normal);
  }
  sync_entity_grips(entity);
  document.scene().set_transform(entity.id, entity.local_transform);
  return {};
}

}  // namespace tamias
