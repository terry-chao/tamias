#pragma once

#include "engine/render/text/glyph_metrics.h"

#include <cstdint>

namespace tamias {

// 布局要的字体度量。抽出接口是为了两件事：
//  1) engine 不依赖任何字体库（stb_truetype 是实现之一，测试用假实现）；
//  2) 布局可以在没有 GPU、没有字体文件的情况下单测。
class GlyphMetricsProvider {
 public:
  virtual ~GlyphMetricsProvider() = default;

  [[nodiscard]] virtual GlyphMetrics metrics(std::uint32_t codepoint, float px_size) const = 0;
  // 字号 px_size 时的自然行高（不含 TextStyle::line_spacing）。
  [[nodiscard]] virtual float line_height(float px_size) const = 0;
  // 基线到字面顶部的距离；第一行的基线落在这里。
  [[nodiscard]] virtual float ascent(float px_size) const = 0;
  // 字距调整；没有就返回 0（不影响布局正确性，只影响松紧）。
  [[nodiscard]] virtual float kerning(std::uint32_t left, std::uint32_t right,
                                      float px_size) const {
    (void)left;
    (void)right;
    (void)px_size;
    return 0.f;
  }
};

}  // namespace tamias
