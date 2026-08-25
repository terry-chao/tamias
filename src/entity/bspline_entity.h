#pragma once

#include "entity/sketch_entity.h"

#include <vector>

namespace tamias {

class BSplineEntity final : public SketchEntity {
 public:
  BSplineEntity() : SketchEntity(EntityKind::BSpline) {}
  explicit BSplineEntity(std::vector<Vec3> points, int degree = 0);
};

}  // namespace tamias
