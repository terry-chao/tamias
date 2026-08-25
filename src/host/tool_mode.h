#pragma once

namespace tamias {

// 当前激活的创建工具。桌面与 web 共用同一枚举。
enum class ToolMode {
  None,
  Wall,
  Box,
  Cylinder,
  Beam,
  Column,
  Slab,
  Door,
  Window,
  Line,
  Polyline,
  Circle,
  Arc,
  Bezier,
  Rectangle
};

}  // namespace tamias
