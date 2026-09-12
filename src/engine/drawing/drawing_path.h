#pragma once

#include "engine/math/math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

// 图元类型。翻模要靠它区分「这根线是墙」和「这是个柱子 / 门窗符号」：
// 圆多半是柱，闭合多段线可能是墙轮廓或板边界，块引用是门窗。
enum class DrawingPathKind : std::uint8_t {
  Line = 0,      // LINE（两点的直线段）
  Polyline = 1,  // LWPOLYLINE / POLYLINE（圆弧已按凸度离散成折线）
  Circle = 2,    // CIRCLE
  Arc = 3,       // ARC
  Ellipse = 4,   // ELLIPSE
};

// 图纸里的一条二维路径：连续点列（圆弧/圆等已在解析时按公差离散）。
// color 是解析后的 RGB（BYLAYER 已取图层色，真彩色优先），不是原始 ACI 索引。
struct DrawingPath {
  std::vector<Vec2> points;
  bool closed = false;
  std::uint32_t layer = 0;          // Drawing::layers() 的索引
  Vec3 color{0.f, 0.f, 0.f};
  DrawingPathKind kind = DrawingPathKind::Polyline;
  // 所在块名（不在块里为空）。门窗图例是块引用，靠它认出 "M0921" 这类编号。
  std::string block;
  // 图元标高（图纸单位，即 DXF 的 z）。多层画在一张图里时用它分层。
  float elevation = 0.f;
};

}  // namespace tamias
