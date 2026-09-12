#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <string>

namespace tamias {

// 图纸里的一段单行文字。position 是插入点（世界/图纸坐标，Y 向上）。
struct DrawingText {
  Vec2 position{};
  float height = 2.5f;       // 字高，图纸单位
  float rotation_deg = 0.f;  // 逆时针，度
  std::string text;          // 原样字节：源文件不是 UTF-8 时可能乱码（见 docs/DRAWING.md）
  std::uint32_t layer = 0;
  Vec3 color{0.f, 0.f, 0.f};
};

}  // namespace tamias
