#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <string>

namespace tamias {

// 轴网的一根轴线。它是**平面定位参考**，不是三维构件：端点恒在 y = 0 平面上，
// 显示高度由视口决定（跟随当前楼层标高）。
enum class GridAxisDirection : std::uint8_t {
  AlongZ = 0,  // 固定 x、沿 Z 延伸 —— 平面图上的竖直轴，编号轴 ①②③
  AlongX = 1,  // 固定 z、沿 X 延伸 —— 平面图上的水平轴，字母轴 Ⓐ Ⓑ
};

// 一根轴 = 方向 + 固定坐标 + 沿轴范围。四点定位（两个位置 + 两个端点）比
// 「任意两端点」更贴合轴网语义：改一根轴的位置不会把它的走向也改掉。
struct GridAxis {
  std::uint64_t id = 0;
  std::string name;  // "1" / "A"
  GridAxisDirection direction = GridAxisDirection::AlongZ;
  double position = 0.0;  // 固定坐标（米）：AlongZ 是 x，AlongX 是 z
  double start = 0.0;     // 沿轴方向的起点（米）
  double end = 0.0;       // 沿轴方向的终点（米）

  [[nodiscard]] Vec3 start_point() const {
    const float p = static_cast<float>(position);
    const float s = static_cast<float>(start);
    return direction == GridAxisDirection::AlongZ ? Vec3{p, 0.f, s} : Vec3{s, 0.f, p};
  }

  [[nodiscard]] Vec3 end_point() const {
    const float p = static_cast<float>(position);
    const float e = static_cast<float>(end);
    return direction == GridAxisDirection::AlongZ ? Vec3{p, 0.f, e} : Vec3{e, 0.f, p};
  }

  [[nodiscard]] double length() const { return end - start; }

  // 平面（XZ）上轴线的固定坐标：AlongZ 是 x，AlongX 是 z。
  [[nodiscard]] double plan_offset() const { return position; }
};

}  // namespace tamias
