#pragma once

#include "entity/sketch_entity.h"

namespace tamias {

class CircleEntity final : public SketchEntity {
 public:
  CircleEntity() : SketchEntity(EntityKind::Circle) {}
  CircleEntity(Vec3 center, double radius);
};

}  // namespace tamias
