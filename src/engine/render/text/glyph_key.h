#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace tamias {

// 图集条目键：字体 + 码点 + 像素字号。
// 字号是浮点，13.999 和 14.001 会各占一份——用 quantize_glyph_px() 归整以后再当键。
struct GlyphKey {
  std::uint32_t font_id = 0;
  std::uint32_t codepoint = 0;
  float px_size = 0.f;

  [[nodiscard]] bool operator==(const GlyphKey& other) const {
    return font_id == other.font_id && codepoint == other.codepoint && px_size == other.px_size;
  }
};

// 归整到 1/4 像素：字号差 0.25 px 以内共用一张字形，肉眼看不出来，
// 却能把图集命中率提上去（缩放动画每帧都会算出一串小数）。
[[nodiscard]] inline float quantize_glyph_px(float px) {
  return static_cast<float>(static_cast<int>(px * 4.f + 0.5f)) * 0.25f;
}

struct GlyphKeyHash {
  std::size_t operator()(const GlyphKey& key) const {
    std::size_t h = static_cast<std::size_t>(key.font_id) * 0x9E3779B97F4A7C15ull;
    h ^= static_cast<std::size_t>(key.codepoint) + 0x9E3779B9u + (h << 6) + (h >> 2);
    h ^= std::hash<float>{}(key.px_size) + 0x9E3779B9u + (h << 6) + (h >> 2);
    return h;
  }
};

}  // namespace tamias
