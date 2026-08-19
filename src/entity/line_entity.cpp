#include "entity/line_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

LineEntity::LineEntity(Vec3 start, Vec3 end) : SketchEntity(EntityKind::Line) {
  name = "line";
  model.add_feature(FeatureKind::Line, {}, line_feature_params(start, end));
}

}  // namespace tamias
