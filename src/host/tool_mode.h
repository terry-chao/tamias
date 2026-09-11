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
  Rectangle,
  BSpline,
  // 新增构件（建筑/结构分类后补齐）。
  StructuralWall,  // 结构墙 / 剪力墙
  Foundation,      // 基础
  CurtainWall      // 幕墙（建筑）
};

}  // namespace tamias
