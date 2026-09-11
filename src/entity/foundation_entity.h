#pragma once

#include "entity/family_entity.h"

namespace tamias {

// 基础子类型。
enum class FoundationShape : std::uint8_t {
  Isolated = 0,  // 独立基础（矩形块，默认）
  Strip = 1,       // 条形基础（长矩形块）
  Raft = 2,        // 筏板基础（大平板）
  Pile = 3         // 桩基础（圆柱）
};

// 基础：底部结构构件。独立/条形/筏板为矩形截面，桩为圆形截面。
class FoundationEntity final : public FamilyEntity {
 public:
  FoundationEntity() : FamilyEntity(EntityKind::Foundation, "Isolated Footing") {}
  explicit FoundationEntity(Vec3 position, double length = 1.5, double width = 1.5,
                             double height = 0.5);
  // 桩基础：圆柱，直径 × 高度。
  static FoundationEntity pile(Vec3 position, double diameter = 0.6, double height = 3.0);
};

}  // namespace tamias
