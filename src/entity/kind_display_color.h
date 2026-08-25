#pragma once

#include "entity/entity.h"

namespace tamias {

// 着色模式用的构件识别色（不读材质）。饱和度接近、明度接近，避免彩虹色块。
[[nodiscard]] inline Vec3 display_color_for_kind(EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return {0.79f, 0.70f, 0.56f};  // 石灰岩
    case EntityKind::Column:
      return {0.62f, 0.45f, 0.72f};  // 灰紫
    case EntityKind::Slab:
      return {0.90f, 0.78f, 0.38f};  // 赭黄
    case EntityKind::Beam:
      return {0.82f, 0.50f, 0.38f};  // 陶土
    case EntityKind::Door:
      return {0.42f, 0.58f, 0.48f};  // 鼠尾绿
    case EntityKind::Window:
      return {0.48f, 0.70f, 0.78f};  // 浅水蓝
    case EntityKind::Box:
      return {0.62f, 0.68f, 0.76f};  // 冷灰石
    case EntityKind::Cylinder:
      return {0.46f, 0.64f, 0.62f};  // 青石
    case EntityKind::Line:
    case EntityKind::Polyline:
    case EntityKind::Circle:
    case EntityKind::Arc:
    case EntityKind::Bezier:
    case EntityKind::Rectangle:
    case EntityKind::BSpline:
    case EntityKind::Nurbs:
      return {0.18f, 0.80f, 0.98f};  // 草图青
  }
  return {0.72f, 0.74f, 0.78f};
}

inline constexpr Vec3 kImportDisplayColor{0.72f, 0.74f, 0.78f};

}  // namespace tamias
