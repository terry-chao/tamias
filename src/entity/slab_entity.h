#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 板：水平构件，矩形平面，厚度方向沿 Y。
class SlabEntity final : public FamilyEntity {
 public:
  SlabEntity() : FamilyEntity(EntityKind::Slab, "Concrete Slab") {}
  explicit SlabEntity(Vec3 position, double length = 4.0, double width = 3.0,
                      double thickness = 0.2);
};

}  // namespace tamias
