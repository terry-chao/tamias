#pragma once

#include "entity/sketch_entity.h"

#include <vector>

namespace tamias {

class BezierEntity final : public SketchEntity {
 public:
  BezierEntity() : SketchEntity(EntityKind::Bezier) {}
  explicit BezierEntity(std::vector<Vec3> points);
  BezierEntity(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3);
};

}  // namespace tamias
