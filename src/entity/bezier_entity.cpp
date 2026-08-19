#include "entity/bezier_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

BezierEntity::BezierEntity(std::vector<Vec3> points) : SketchEntity(EntityKind::Bezier) {
  name = "bezier";
  model.add_feature(FeatureKind::Bezier, {}, bezier_feature_params(std::move(points)));
}

BezierEntity::BezierEntity(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3)
    : BezierEntity(std::vector<Vec3>{p0, p1, p2, p3}) {}

}  // namespace tamias
