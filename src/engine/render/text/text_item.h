#pragma once

#include "engine/math/math.h"
#include "engine/render/text/text_align.h"
#include "engine/render/text/text_kind.h"
#include "engine/render/text/text_space.h"
#include "engine/render/text/text_style.h"

#include <cstdint>
#include <string>

namespace tamias {

// 一段文字：内容 + 摆在哪 + 长什么样。渲染侧只吃它（以及布局结果），
// 不认 std::string（见 docs/TEXT.md §2）。
//
// 屏幕空间标注**不进语义树**：它依赖相机投影，是每帧现算的视图产物。
// 模型空间文字有身份（可选中 / 可删 / 属某楼层），会被烤成普通网格进语义树。
struct TextItem {
  std::uint64_t id = 0;  // 拾取 / 脏标记 / 派生去重用
  TextKind kind = TextKind::Annotation;
  TextSpace space = TextSpace::Screen;
  std::string utf8;
  // Screen：投到屏幕的**锚点世界坐标**（跟相机走，缩放不变大小）。
  // World：文字原点（基线左端，或按 anchor 的定义）。
  Vec3 anchor_world{};
  // World 空间的基向量：right = 基线方向，up = 字面向上；法线 = right × up。
  // Screen 空间忽略这两个（永远屏幕水平）。
  Vec3 right{1.f, 0.f, 0.f};
  Vec3 up{0.f, 1.f, 0.f};
  TextAlign align = TextAlign::Left;
  TextStyle style{};
  float max_width = 0.f;  // 0 = 不折行
  bool selected = false;
};

}  // namespace tamias
