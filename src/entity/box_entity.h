#pragma once

#include "entity/entity.h"

namespace tamias {

// 盒子实体：宽 × 高 × 深。
class BoxEntity final : public Entity {
 public:
  BoxEntity() { kind_ = EntityKind::Box; }
  static BoxEntity at(Vec3 position);
};

}  // namespace tamias
