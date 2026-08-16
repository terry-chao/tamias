#include "entity/entity.h"

#include "entity/box_entity.h"
#include "entity/cylinder_entity.h"
#include "entity/wall_entity.h"
#include "engine/modeling/occt_geom_builder.h"

namespace tamias {

std::unique_ptr<Entity> make_entity(EntityKind kind) {
  switch (kind) {
    case EntityKind::Box:
      return std::make_unique<BoxEntity>();
    case EntityKind::Cylinder:
      return std::make_unique<CylinderEntity>();
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
  e->local_transform = local_transform;
  return e;
}

Result<MeshCpu> Entity::createGeom(double deflection) const {
#if defined(TAMIAS_HAS_OCCT)
  return geometry_builder().build(model, deflection);
#else
  (void)deflection;
  return Err("Entity::createGeom requires OCCT");
#endif
}

}  // namespace tamias
