#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 墙实体：墙厚 × 墙高 × 两点间长度。由两个端点构造（drag 交互在 app 层，实体只管数据）。
class WallEntity final : public FamilyEntity {
 public:
  WallEntity() : FamilyEntity(EntityKind::Wall, "Basic Wall") {}
  WallEntity(Vec3 start, Vec3 end, double thickness, double height);
};

}  // namespace tamias
