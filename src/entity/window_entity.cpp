#include "entity/window_entity.h"

namespace tamias {

WindowEntity::WindowEntity(Vec3 position, double width, double height, double thickness) {
  kind_ = EntityKind::Window;
  name = "window";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", thickness}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  local_transform = translate(position);
}

}  // namespace tamias
