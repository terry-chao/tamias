#include "entity/polyline_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

PolylineEntity::PolylineEntity(std::vector<Vec3> points) : SketchEntity(EntityKind::Polyline) {
  name = "polyline";
  model.add_feature(FeatureKind::Polyline, {}, polyline_feature_params(points));
}

}  // namespace tamias
