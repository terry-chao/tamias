#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tamias {

// 一个字形光栅化后的 8 位覆盖度位图（0 = 全透，255 = 全实）。
// bearing_x / bearing_y 是位图相对**笔位 / 基线**的偏移，和 GlyphMetrics 同一套
// 约定：位图左上角 = (pen_x + bearing_x, baseline_y - bearing_y)。
// 空白字形（空格）没有墨迹：width/height 为 0，alpha 为空。
struct GlyphBitmap {
  int width = 0;
  int height = 0;
  float bearing_x = 0.f;
  float bearing_y = 0.f;
  std::vector<std::uint8_t> alpha;  // width * height；行主序

  [[nodiscard]] bool empty() const { return width <= 0 || height <= 0; }
  [[nodiscard]] std::size_t pixel_count() const {
    return static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  }
};

}  // namespace tamias
