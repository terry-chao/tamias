#include "entity/door_entity.h"

namespace tamias {

DoorEntity::DoorEntity(Vec3 position, double width, double height, double thickness,
                       double sill)
    : OpeningEntity(EntityKind::Door, "Single-Flush Door", 0.0) {
  name = "door";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", thickness}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  set_sill_height(sill);
  local_transform = translate(position);
}

}  // namespace tamias
