#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 板：水平构件，矩形平面，厚度方向沿 Y。
class SlabEntity final : public FamilyEntity {
 public:
  SlabEntity() : FamilyEntity(EntityKind::Slab, "Concrete Slab") {}
  explicit SlabEntity(Vec3 position, double length = 4.0, double width = 3.0,
                      double thickness = 0.2);
  // 两个对角点定 XZ 平面上的矩形，厚度沿 Y。
  SlabEntity(Vec3 a, Vec3 b, double thickness);
};

}  // namespace tamias
