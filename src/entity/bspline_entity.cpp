#include "entity/bspline_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

BSplineEntity::BSplineEntity(std::vector<Vec3> points, int degree)
    : SketchEntity(EntityKind::BSpline) {
  name = "bspline";
  model.add_feature(FeatureKind::BSpline, {}, bspline_feature_params(std::move(points), degree));
}

}  // namespace tamias
