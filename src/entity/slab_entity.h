#pragma once

#include "entity/entity.h"

namespace tamias {

// 板：水平构件，矩形平面，厚度方向沿 Y。
class SlabEntity final : public Entity {
 public:
  SlabEntity() { kind_ = EntityKind::Slab; }
  explicit SlabEntity(Vec3 position, double length = 4.0, double width = 3.0,
                      double thickness = 0.2);
};

}  // namespace tamias
