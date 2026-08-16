#include "entity/cylinder_entity.h"

namespace tamias {

CylinderEntity::CylinderEntity(Vec3 position) {
  kind_ = EntityKind::Cylinder;
  name = "cylinder";
  auto& profile = model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", 0.5}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 2.0}});
  local_transform = translate(position);
}

}  // namespace tamias
