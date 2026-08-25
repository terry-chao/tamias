#pragma once

#include "entity/sketch_entity.h"

#include <vector>

namespace tamias {

class NurbsEntity final : public SketchEntity {
 public:
  NurbsEntity() : SketchEntity(EntityKind::Nurbs) {}
  explicit NurbsEntity(std::vector<Vec3> points, std::vector<float> weights = {}, int degree = 0);
};

}  // namespace tamias
