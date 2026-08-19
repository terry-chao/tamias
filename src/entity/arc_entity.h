#pragma once

#include "entity/sketch_entity.h"

namespace tamias {

class ArcEntity final : public SketchEntity {
 public:
  ArcEntity() : SketchEntity(EntityKind::Arc) {}
  ArcEntity(Vec3 start, Vec3 through, Vec3 end);
};

}  // namespace tamias
