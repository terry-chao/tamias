#pragma once

#include "engine/render/text/glyph_bitmap.h"

#include <cstdint>

namespace tamias {

// 把码点光栅成位图。抽出接口的理由和 GlyphMetricsProvider 一样：engine 不绑死某个
// 字体库。实现可以是：
//   - StbFont（engine，桌面 / wasm 通用）；
//   - 壳里的 Qt / 系统字体实现；
//   - 测试里的假实现（不碰字体文件，图集逻辑可以单独测）。
class GlyphRasterizer {
 public:
  virtual ~GlyphRasterizer() = default;

  [[nodiscard]] virtual GlyphBitmap rasterize(std::uint32_t codepoint, float px_size) const = 0;
  // 字体里有没有这个码位；决定「画豆腐块还是留白 + 记一条诊断」。
  [[nodiscard]] virtual bool has_glyph(std::uint32_t codepoint) const {
    (void)codepoint;
    return true;
  }
};

}  // namespace tamias
