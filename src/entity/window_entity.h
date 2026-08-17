#pragma once

#include "entity/entity.h"

namespace tamias {

// 窗：先按独立窗扇/玻璃板建模；墙开洞关系后续接入 Boolean/洞口系统。
class WindowEntity final : public Entity {
 public:
  WindowEntity() { kind_ = EntityKind::Window; }
  explicit WindowEntity(Vec3 position, double width = 1.2, double height = 1.2,
                        double thickness = 0.08);
};

}  // namespace tamias
