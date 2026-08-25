#include "entity/nurbs_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

NurbsEntity::NurbsEntity(std::vector<Vec3> points, std::vector<float> weights, int degree)
    : SketchEntity(EntityKind::Nurbs) {
  name = "nurbs";
  model.add_feature(FeatureKind::Nurbs, {},
                    nurbs_feature_params(std::move(points), std::move(weights), degree));
}

}  // namespace tamias
