#include "entity/box_entity.h"

namespace tamias {

BoxEntity::BoxEntity(Vec3 position) {
  kind_ = EntityKind::Box;
  name = "box";
  auto& profile = model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 1.0}});
  local_transform = translate(position);
}

}  // namespace tamias
