#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 结构墙 / 剪力墙：承重竖向构件，由两个端点 + 厚度 + 高度定义。
// 与建筑墙（Wall）同造型配方，但归属结构专业，用于结构分析与 IFC 的 IfcWall(承重) 映射。
class StructuralWallEntity final : public FamilyEntity {
 public:
  StructuralWallEntity() : FamilyEntity(EntityKind::StructuralWall, "Structural Wall") {}
  StructuralWallEntity(Vec3 start, Vec3 end, double thickness, double height);
};

}  // namespace tamias
