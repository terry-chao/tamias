#include "entity/arc_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

ArcEntity::ArcEntity(Vec3 start, Vec3 through, Vec3 end) : SketchEntity(EntityKind::Arc) {
  name = "arc";
  model.add_feature(FeatureKind::Arc, {}, arc_feature_params(start, through, end));
}

}  // namespace tamias
