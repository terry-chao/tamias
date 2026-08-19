#pragma once

#include "entity/sketch_entity.h"

namespace tamias {

class RectangleEntity final : public SketchEntity {
 public:
  RectangleEntity() : SketchEntity(EntityKind::Rectangle) {}
  RectangleEntity(Vec3 a, Vec3 b);
};

}  // namespace tamias
