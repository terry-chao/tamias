#pragma once

#include "engine/render/text/glyph_metrics_provider.h"
#include "engine/render/text/text_align.h"
#include "engine/render/text/text_line.h"
#include "engine/render/text/text_style.h"

#include <string_view>
#include <vector>

namespace tamias {

// 一段文字排完的样子（相对文字块左上角；y 向下）。
//
// 折行是**贪心**的：拉丁在空格断，中日韩可以在字间断，单个词长过一行就硬断。
// 行尾空白不参与宽度也不产四边形。
struct TextLayout {
  std::vector<TextLine> lines;
  float width = 0.f;           // 自然宽度 = 最长一行
  float height = 0.f;          // 行数 × 行高（行高 = line_spacing × 字体行高）
  float first_baseline = 0.f;  // = 字体 ascent
  float line_height = 0.f;
  std::size_t glyph_count = 0;  // 含空白的字数，方便诊断与预算

  [[nodiscard]] bool empty() const { return lines.empty(); }
};

// utf8 空串 → 空布局（lines 为空，消费方直接跳过不画）。
// max_width <= 0 = 不折行（仍然在 '\n' 处断行）。
// align 的参考宽度：max_width > 0 时是 max_width，否则是自然宽度。
[[nodiscard]] TextLayout layout_text(std::string_view utf8, const TextStyle& style,
                                     TextAlign align, float max_width,
                                     const GlyphMetricsProvider& metrics);

}  // namespace tamias
