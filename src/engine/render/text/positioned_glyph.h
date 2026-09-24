#pragma once

#include "engine/render/text/glyph_metrics.h"

#include <cstdint>

namespace tamias {

// 布局结果里的一个字：码点 + 笔位 + 度量。
// 坐标系：文字块左上角为原点，x 向右、y **向下**（和屏幕一致）。
// y 是该行的**基线**，位图矩形 = (x + bearing_x, y - bearing_y, width, height)。
// World 空间消费方再拿 right/up 把 (x, y) 映到世界平面。
struct PositionedGlyph {
  std::uint32_t codepoint = 0;
  float x = 0.f;
  float y = 0.f;
  GlyphMetrics metrics{};
};

}  // namespace tamias
