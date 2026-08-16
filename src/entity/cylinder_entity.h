#pragma once

#include "entity/entity.h"

namespace tamias {

// 圆柱实体：半径 × 高，Y 轴为轴。
class CylinderEntity final : public Entity {
 public:
  CylinderEntity() { kind_ = EntityKind::Cylinder; }
  explicit CylinderEntity(Vec3 position);
};

}  // namespace tamias
