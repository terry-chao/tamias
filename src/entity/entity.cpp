#include "entity/entity.h"

#include "entity/arc_entity.h"
#include "entity/bezier_entity.h"
#include "entity/box_entity.h"
#include "entity/beam_entity.h"
#include "entity/circle_entity.h"
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
  e->local_transform = local_transform;
  e->grips = grips;
  if (auto* family = dynamic_cast<FamilyEntity*>(e.get())) {
    if (const auto* self_family = dynamic_cast<const FamilyEntity*>(this)) {
      family->set_family_type(self_family->family_type());
    }
  }
  return e;
}

Result<MeshCpu> Entity::createGeom(double deflection) const {
  return geometry_builder().build(model, deflection);
}

}  // namespace tamias
