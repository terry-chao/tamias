#pragma once

// tac 的基础值类型：与任何具体界面库无关。
//
// 硬规则：src/ui/tac/** 不得 include 任何 Qt 头（见 docs/TAC.md 与
// scripts/check-tac-boundary.ps1）。Qt 只出现在 src/ui/qt/** 与迁移期内的
// src/app/**。

#include <cstdint>

namespace tac {

struct Point {
  float x = 0.0F;
  float y = 0.0F;
};

struct Size {
  float width = 0.0F;
  float height = 0.0F;
};

struct Rect {
  float x = 0.0F;
  float y = 0.0F;
  float width = 0.0F;
  float height = 0.0F;

  [[nodiscard]] constexpr float right() const { return x + width; }
  [[nodiscard]] constexpr float bottom() const { return y + height; }
  [[nodiscard]] constexpr Size size() const { return {width, height}; }
  [[nodiscard]] constexpr bool empty() const { return width <= 0.0F || height <= 0.0F; }
  [[nodiscard]] constexpr bool contains(Point p) const {
    return p.x >= x && p.y >= y && p.x < right() && p.y < bottom();
  }
};

struct Margins {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
};

// 8 位 sRGB + alpha，和 QColor / CSS 的常见写法一一对应。
struct Color {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
  std::uint8_t a = 255;

  [[nodiscard]] static constexpr Color rgba(std::uint8_t red, std::uint8_t green,
                                            std::uint8_t blue, std::uint8_t alpha = 255) {
    return Color{red, green, blue, alpha};
  }

  [[nodiscard]] constexpr Color with_alpha(std::uint8_t alpha) const {
    return Color{r, g, b, alpha};
  }

  [[nodiscard]] friend constexpr bool operator==(const Color&, const Color&) = default;
};

}  // namespace tac
