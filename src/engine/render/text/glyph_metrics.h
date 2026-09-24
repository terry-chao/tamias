#pragma once

namespace tamias {

// 一个字形的度量。px_size 下算出来，单位是像素（World 空间的字最后也归到像素
// 布局：先按世界字高 × 该比例换算成像素，再走同一套布局）。
struct GlyphMetrics {
  float advance = 0.f;    // 前进宽度（笔位推进量）
  float bearing_x = 0.f;  // 位图左边相对笔位
  float bearing_y = 0.f;  // 位图顶部相对基线，**向上为正**
  float width = 0.f;      // 位图宽
  float height = 0.f;     // 位图高
  bool whitespace = false;  // 空白：不产四边形，但仍推进笔位
  bool valid = false;       // false = 字体里没有这个码位（画 tofu 并记诊断）
};

}  // namespace tamias
