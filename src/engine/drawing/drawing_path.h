#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

// 图纸里的一条二维路径：连续点列（圆弧/圆等已在解析时按公差离散）。
// color 是解析后的 RGB（BYLAYER 已取图层色，真彩色优先），不是原始 ACI 索引。
struct DrawingPath {
  std::vector<Vec2> points;
  bool closed = false;
  std::uint32_t layer = 0;          // Drawing::layers() 的索引
  Vec3 color{0.f, 0.f, 0.f};
};

}  // namespace tamias
