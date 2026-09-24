#pragma once

#include "engine/math/math.h"
#include "engine/render/text/text_align.h"
#include "engine/render/text/text_kind.h"

#include <cstdint>
#include <string>

namespace tamias {

// 用户放的一段文字注记：**世界锚点 + 屏幕朝向**（billboard）。
//
// 为什么不做成三角网实体：BIM 里的注记绝大多数是「视图朝向、大小恒定」的，不是刻在
// 板上的字。字形轮廓（带孔洞）三角化要么上内核、要么写健壮的多边形三角化，而它只服务
// 「文字要被遮挡 / 参与布尔 / 导出 OBJ」这一小类需求——那类留作 P3b（见 docs/TEXT.md §4.2）。
//
// 和 GridAxis 一样：它是文档数据（随 .tdoc 存），但不是构件（没有 Entity / SceneNode，
// 也没有网格）。
struct TextAnnotation {
  std::uint64_t id = 0;  // Document 分配的 id
  TextKind kind = TextKind::Annotation;
  std::string text;
  Vec3 anchor{};         // 世界锚点（点击处的工作平面点）
  float size_px = 14.f;  // 屏幕字高，**逻辑像素**；渲染时乘 DPR
  Vec3 color{0.94f, 0.96f, 0.99f};
  float opacity = 1.f;
  TextAlign align = TextAlign::Left;
  // 选中（编辑器状态，不落盘；和 GridAxis::selected 同理）。
  bool selected = false;
};

}  // namespace tamias
