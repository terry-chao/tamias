#pragma once

#include "bim/grid_axis.h"

#include <string>
#include <vector>

namespace tamias {

// 轴网尺寸链的一格：相邻两根轴线之间的距离。
// 信息全从轴网现算（不落盘）——轴线挪了，尺寸自动跟着变。
struct GridDimension {
  std::string from_name;
  std::string to_name;
  double distance = 0.0;  // 米
  // 标注锚点：两轴中间、落在该方向轴网的外缘（平面图里尺寸链画在建筑外侧）。
  Vec3 anchor{};
};

// 沿 direction 取相邻轴线间距：
//   AlongZ（固定 x、沿 z 延伸的竖轴 ①②③）→ 量的是 x 方向间距，链在 z 外缘；
//   AlongX（固定 z、沿 x 延伸的横轴 Ⓐ Ⓑ）→ 量的是 z 方向间距，链在 x 外缘。
// 少于两根轴返回空。anchor 的 y 恒为 0（视口按当前楼层标高抬起来）。
[[nodiscard]] std::vector<GridDimension> grid_dimension_chain(
    const std::vector<GridAxis>& axes, GridAxisDirection direction);

}  // namespace tamias
