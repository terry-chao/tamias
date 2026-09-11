#pragma once

#include "entity/opening_entity.h"

namespace tamias {

// 窗：开洞实体，点在墙上时写入 HostedOn 关联，随墙重造型并切穿宿主墙。
class WindowEntity final : public OpeningEntity {
 public:
  WindowEntity() : OpeningEntity(EntityKind::Window, "Fixed Window", 0.9) {}
  explicit WindowEntity(Vec3 position, double width = 1.2, double height = 1.2,
                        double thickness = 0.08, double sill = 0.9);
};

}  // namespace tamias
