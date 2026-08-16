#include "entity/box_entity.h"

namespace tamias {

BoxEntity BoxEntity::at(Vec3 position) {
  BoxEntity box;
  box.name = "box";
  auto& profile =
      box.model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}});
  box.model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 1.0}});
  box.local_transform = translate(position);
  return box;
}

}  // namespace tamias
