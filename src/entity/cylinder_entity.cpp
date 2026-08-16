#include "entity/cylinder_entity.h"

namespace tamias {

CylinderEntity CylinderEntity::at(Vec3 position) {
  CylinderEntity cylinder;
  cylinder.name = "cylinder";
  auto& profile = cylinder.model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", 0.5}});
  cylinder.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 2.0}});
  cylinder.local_transform = translate(position);
  return cylinder;
}

}  // namespace tamias
