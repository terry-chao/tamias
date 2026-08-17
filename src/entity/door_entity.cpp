#include "entity/door_entity.h"

namespace tamias {

DoorEntity::DoorEntity(Vec3 position, double width, double height, double thickness)
    : FamilyEntity(EntityKind::Door, "Single-Flush Door") {
  name = "door";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", thickness}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  local_transform = translate(position);
}

}  // namespace tamias
