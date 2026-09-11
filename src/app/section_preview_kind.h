#pragma once

#include <cstdint>

namespace tamias {

// 绘制面板截面预览的几何种类。
enum class SectionPreviewKind : std::uint8_t {
  None = 0,
  Rectangle,   // 矩形截面：水平 × 竖直
  Circle,      // 圆形截面：直径
  Tee,         // T 形梁
  IBeam,       // 工字梁
  HollowRect,  // 空心墙：外框 − 内腔
  Window,      // 窗立面：矩形 + 分格
  Door,        // 门立面：矩形 + 门扇示意
  Curtain      // 幕墙剖面：矩形 + 分格
};

}  // namespace tamias
