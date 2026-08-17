#pragma once

#include "entity/entity.h"

namespace tamias {

// 门：先按独立门扇建模；墙开洞关系后续接入 Boolean/洞口系统。
class DoorEntity final : public Entity {
 public:
  DoorEntity() { kind_ = EntityKind::Door; }
  explicit DoorEntity(Vec3 position, double width = 1.0, double height = 2.1,
                      double thickness = 0.05);
};

}  // namespace tamias
