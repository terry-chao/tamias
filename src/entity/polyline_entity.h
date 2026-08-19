#pragma once

#include "entity/sketch_entity.h"

#include <vector>

namespace tamias {

class PolylineEntity final : public SketchEntity {
 public:
  PolylineEntity() : SketchEntity(EntityKind::Polyline) {}
  explicit PolylineEntity(std::vector<Vec3> points);
};

}  // namespace tamias
