#include "entity/circle_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

CircleEntity::CircleEntity(Vec3 center, double radius) : SketchEntity(EntityKind::Circle) {
  name = "circle";
  model.add_feature(FeatureKind::CircleWire, {}, circle_wire_params(center, radius));
}

}  // namespace tamias
