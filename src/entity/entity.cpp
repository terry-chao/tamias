#include "entity/entity.h"

#include "entity/arc_entity.h"
#include "entity/bezier_entity.h"
#include "entity/box_entity.h"
#include "entity/beam_entity.h"
#include "entity/bspline_entity.h"
#include "entity/circle_entity.h"
#include "entity/column_entity.h"
#include "entity/cylinder_entity.h"
#include "entity/door_entity.h"
#include "entity/family_entity.h"
#include "entity/line_entity.h"
#include "entity/nurbs_entity.h"
#include "entity/polyline_entity.h"
#include "entity/rectangle_entity.h"
#include "entity/slab_entity.h"
#include "entity/wall_entity.h"
#include "entity/window_entity.h"
#include "entity/column_entity.h"
#include "entity/cylinder_entity.h"
#include "entity/door_entity.h"
#include "entity/family_entity.h"
#include "entity/line_entity.h"
#include "entity/polyline_entity.h"
#include "entity/rectangle_entity.h"
#include "entity/slab_entity.h"
#include "entity/wall_entity.h"
#include "entity/window_entity.h"
#include "engine/modeling/occt_geom_builder.h"
#include "bim/line_location.h"
#include "bim/point_location.h"
#include "bim/surface_location.h"

namespace tamias {

std::unique_ptr<Entity> make_entity(EntityKind kind) {
  switch (kind) {
    case EntityKind::Box:
      return std::make_unique<BoxEntity>();
    case EntityKind::Cylinder:
      return std::make_unique<CylinderEntity>();
    case EntityKind::Beam:
      return std::make_unique<BeamEntity>();
    case EntityKind::Column:
      return std::make_unique<ColumnEntity>();
    case EntityKind::Slab:
      return std::make_unique<SlabEntity>();
    case EntityKind::Door:
      return std::make_unique<DoorEntity>();
    case EntityKind::Window:
      return std::make_unique<WindowEntity>();
    case EntityKind::Line:
      return std::make_unique<LineEntity>();
    case EntityKind::Polyline:
      return std::make_unique<PolylineEntity>();
    case EntityKind::Circle:
      return std::make_unique<CircleEntity>();
    case EntityKind::Arc:
      return std::make_unique<ArcEntity>();
    case EntityKind::Bezier:
      return std::make_unique<BezierEntity>();
    case EntityKind::Rectangle:
      return std::make_unique<RectangleEntity>();
    case EntityKind::BSpline:
      return std::make_unique<BSplineEntity>();
    case EntityKind::Nurbs:
      return std::make_unique<NurbsEntity>();
    case EntityKind::Wall:
    default:
      return std::make_unique<WallEntity>();
  }
}

std::unique_ptr<Entity> Entity::clone() const {
  auto e = make_entity(kind_);
  e->id = id;
  e->name = name;
  e->model = model;
  e->mesh_asset_id = mesh_asset_id;
  e->material_id = material_id;
  e->location = location ? location->clone() : nullptr;
  e->local_transform = local_transform;
  e->grips = grips;
  if (auto* family = dynamic_cast<FamilyEntity*>(e.get())) {
    if (const auto* self_family = dynamic_cast<const FamilyEntity*>(this)) {
      family->set_family_type(self_family->family_type());
    }
  }
  return e;
}

void Entity::sync_from_location(double storey_elevation) {
  if (!location) {
    return;
  }
  local_transform = location->transform(storey_elevation);
  if (kind_ != EntityKind::Wall || location->kind() != LocationKind::Line) {
    return;
  }
  const auto* line = static_cast<const LineLocation*>(location.get());
  for (Feature& feature : model.features()) {
    if (feature.kind == FeatureKind::RectProfile) {
      model.set_param(feature.id, "height", line->length());
      break;
    }
  }
}

void Entity::sync_location_from_transform(const Mat4& transform, double storey_elevation) {
  local_transform = transform;
  if (!location) {
    return;
  }
  location->set_elevation_offset(static_cast<double>(transform(1, 3)) - storey_elevation);
  switch (location->kind()) {
    case LocationKind::Point:
      static_cast<PointLocation*>(location.get())
          ->set_point({transform(0, 3), 0.f, transform(2, 3)});
      break;
    case LocationKind::Line: {
      auto* line = static_cast<LineLocation*>(location.get());
      double length = line->length();
      for (const Feature& feature : model.features()) {
        if (feature.kind == FeatureKind::RectProfile) {
          length = model.param(feature.id, "height", length);
          break;
        }
      }
      const float half = static_cast<float>(length * 0.5);
      Vec3 start = transform * Vec3{0.f, 0.f, -half};
      Vec3 end = transform * Vec3{0.f, 0.f, half};
      line->set_start(start);
      line->set_end(end);
      break;
    }
    case LocationKind::Surface: {
      auto* surface = static_cast<SurfaceLocation*>(location.get());
      surface->set_origin({transform(0, 3), 0.f, transform(2, 3)});
      surface->set_x_axis({transform(0, 0), 0.f, transform(2, 0)});
      break;
    }
  }
}

Result<MeshCpu> Entity::createGeom(double deflection) const {
  return geometry_builder().build(model, deflection);
}

}  // namespace tamias
