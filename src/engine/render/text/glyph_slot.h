#pragma once

namespace tamias {

// 字形在图集里的位置与度量。
// x/y/width/height 是**像素**矩形（图集扩容时会保持不变）；uv 是归一化坐标，
// 扩容会重算。绘制时：四边形 = (pen_x + bearing_x, baseline_y - bearing_y, w, h)。
struct GlyphSlot {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
  float u0 = 0.f;
  float v0 = 0.f;
  float u1 = 0.f;
  float v1 = 0.f;
  float bearing_x = 0.f;
  float bearing_y = 0.f;
};

}  // namespace tamias
