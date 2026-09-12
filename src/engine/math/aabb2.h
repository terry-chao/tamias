#pragma once

#include <algorithm>

namespace tamias {

// 二维轴对齐包围盒。默认是"空盒"：valid() == false，直到 expand 过。
struct Aabb2 {
  float min_x = 1e30f;
  float min_y = 1e30f;
  float max_x = -1e30f;
  float max_y = -1e30f;

  void expand(float x, float y) {
    min_x = std::min(min_x, x);
    min_y = std::min(min_y, y);
    max_x = std::max(max_x, x);
    max_y = std::max(max_y, y);
  }

  [[nodiscard]] bool valid() const { return min_x <= max_x && min_y <= max_y; }
  [[nodiscard]] float width() const { return valid() ? max_x - min_x : 0.f; }
  [[nodiscard]] float height() const { return valid() ? max_y - min_y : 0.f; }
};

}  // namespace tamias
