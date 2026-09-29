#pragma once

#include "entity/family/family_entity.h"

namespace tamias {

// 幕墙：建筑外墙，由两个端点 + 高度定义，厚度由竖梃截面预留（先按薄板处理）。
class CurtainWallEntity final : public FamilyEntity {
 public:
  CurtainWallEntity() : FamilyEntity(EntityKind::CurtainWall, "Curtain Wall") {}
  CurtainWallEntity(Vec3 start, Vec3 end, double thickness, double height);
};

}  // namespace tamias
