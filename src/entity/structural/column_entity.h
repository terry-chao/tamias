#pragma once

#include "entity/family/family_entity.h"

namespace tamias {

// 柱截面子类型。绘制面板先选子类型，再给参数。
enum class ColumnShape : std::uint8_t {
  Rectangular = 0,  // 矩形柱（默认）
  Circular = 1       // 圆柱
};

// 柱：竖向结构构件，底部中心由 position 指定。
// 矩形柱由 width×depth 定义截面，圆柱由 diameter 定义截面，沿高度方向拉伸。
class ColumnEntity final : public FamilyEntity {
 public:
  ColumnEntity() : FamilyEntity(EntityKind::Column, "Concrete Column") {}
  // 矩形柱：width × depth 截面，height 高。
  ColumnEntity(Vec3 position, double width = 0.4, double depth = 0.4,
               double height = 3.0);
  // 圆柱：diameter 截面，height 高。
  static ColumnEntity circular(Vec3 position, double diameter = 0.4,
                               double height = 3.0);
};

}  // namespace tamias
