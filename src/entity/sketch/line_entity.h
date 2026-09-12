#pragma once

#include "entity/sketch/sketch_entity.h"

namespace tamias {

class LineEntity final : public SketchEntity {
 public:
  LineEntity() : SketchEntity(EntityKind::Line) {}
  LineEntity(Vec3 start, Vec3 end);
};

}  // namespace tamias
