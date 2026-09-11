#pragma once

#include "entity/opening_entity.h"

namespace tamias {

// 门：开洞实体，点在墙上时写入 HostedOn 关联，随墙重造型并切穿宿主墙。
class DoorEntity final : public OpeningEntity {
 public:
  DoorEntity() : OpeningEntity(EntityKind::Door, "Single-Flush Door", 0.0) {}
  explicit DoorEntity(Vec3 position, double width = 1.0, double height = 2.1,
                      double thickness = 0.05, double sill = 0.0);
};

}  // namespace tamias
