#include "entity/rectangle_entity.h"

#include "engine/modeling/curve_geom.h"

namespace tamias {

RectangleEntity::RectangleEntity(Vec3 a, Vec3 b) : SketchEntity(EntityKind::Rectangle) {
  name = "rectangle";
  model.add_feature(FeatureKind::RectWire, {}, rect_wire_params(a, b));
}

}  // namespace tamias
