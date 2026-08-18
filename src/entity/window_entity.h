#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 窗：点在墙上时写入 HostedOn 关联，随墙重造型；开洞布尔后续接入。
class WindowEntity final : public FamilyEntity {
 public:
  WindowEntity() : FamilyEntity(EntityKind::Window, "Fixed Window") {}
  explicit WindowEntity(Vec3 position, double width = 1.2, double height = 1.2,
                        double thickness = 0.08);
};

}  // namespace tamias
