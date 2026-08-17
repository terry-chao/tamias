#include "entity/column_entity.h"

namespace tamias {

ColumnEntity::ColumnEntity(Vec3 position, double width, double depth, double height) {
  kind_ = EntityKind::Column;
  name = "column";
  auto& profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", depth}});
  model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", height}});
  local_transform = translate(position);
}

}  // namespace tamias
