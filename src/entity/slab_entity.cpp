#include "entity/slab_entity.h"

namespace tamias {

SlabEntity::SlabEntity(Vec3 position, double length, double width, double thickness)
    : FamilyEntity(EntityKind::Slab, "Concrete Slab") {
  name = "slab";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", length}, {"height", width}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", thickness}});
  local_transform = translate(position);
}

}  // namespace tamias
