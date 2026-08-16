#include "engine/document/entity.h"

#include "engine/modeling/occt_geom_builder.h"

#include <algorithm>
#include <cmath>

namespace tamias {

std::unique_ptr<Entity> make_entity(EntityKind kind) {
  switch (kind) {
    case EntityKind::Box:
      return std::make_unique<Box>();
    case EntityKind::Cylinder:
      return std::make_unique<Cylinder>();
    case EntityKind::Wall:
    default:
      return std::make_unique<Wall>();
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

Wall Wall::drag(Vec3 start, Vec3 end, double thickness, double height) {
  Wall wall;
  wall.name = "wall";
  const Vec3 d = end - start;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start + end) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  // 墙 = RectProfile(width=墙厚, height=墙长) + Extrude(depth=墙高)。
  // 求值器 Z-up→Y-up 后：X=厚、Y=高、Z=长。
  auto& profile = wall.model.add_feature(FeatureKind::RectProfile, {},
                                         {{"width", thickness}, {"height", static_cast<double>(length)}});
  wall.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  wall.local_transform = translate(mid) * rotate_y(yaw);
  return wall;
}

Box Box::at(Vec3 position) {
  Box box;
  box.name = "box";
  auto& profile =
      box.model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}});
  box.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 1.0}});
  box.local_transform = translate(position);
  return box;
}

Cylinder Cylinder::at(Vec3 position) {
  Cylinder cylinder;
  cylinder.name = "cylinder";
  auto& profile = cylinder.model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", 0.5}});
  cylinder.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 2.0}});
  cylinder.local_transform = translate(position);
  return cylinder;
}

}  // namespace tamias
