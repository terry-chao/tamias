#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 门：点在墙上时写入 HostedOn 关联，随墙重造型；开洞布尔后续接入。
class DoorEntity final : public FamilyEntity {
 public:
  DoorEntity() : FamilyEntity(EntityKind::Door, "Single-Flush Door") {}
  explicit DoorEntity(Vec3 position, double width = 1.0, double height = 2.1,
                      double thickness = 0.05);
};

}  // namespace tamias
