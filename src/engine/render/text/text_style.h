#pragma once

#include "engine/math/math.h"
#include "engine/render/text/font_ref.h"

namespace tamias {

// 一段文字的样式。size 的单位跟 TextSpace 走：Screen 是像素字高（em），
// World 是米。padding / letter_spacing 同理。
struct TextStyle {
  FontRef font{};
  float size = 14.f;    // Screen：像素；World：米
  float line_spacing = 1.2f;  // 行距倍数（乘字体自身的行高）
  Vec3 color{0.90f, 0.92f, 0.95f};
  float opacity = 1.f;
  // 垫底底板：标注读数压着模型时保证可读。
  bool pill = false;
  Vec3 pill_color{0.f, 0.f, 0.f};
  float pill_padding = 3.f;  // 与 size 同单位
  float letter_spacing = 0.f;
};

}  // namespace tamias
