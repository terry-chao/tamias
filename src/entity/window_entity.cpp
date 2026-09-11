#include "entity/window_entity.h"

namespace tamias {

WindowEntity::WindowEntity(Vec3 position, double width, double height, double thickness,
                           double sill)
    : OpeningEntity(EntityKind::Window, "Fixed Window", 0.9) {
  name = "window";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", thickness}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  set_sill_height(sill);
  local_transform = translate(position);
}

}  // namespace tamias
