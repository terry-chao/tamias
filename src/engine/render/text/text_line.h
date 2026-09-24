#pragma once

#include "engine/render/text/positioned_glyph.h"

#include <cstddef>
#include <vector>

namespace tamias {

// 布局后的一行。begin/end 是该行在**码点流**里的范围（不含被吃掉的换行符与
// 行尾空白），改文字要局部重排时按它定位。
struct TextLine {
  std::vector<PositionedGlyph> glyphs;
  float width = 0.f;     // 该行字宽（不含行尾空白）
  float baseline = 0.f;  // 相对文字块左上角，向下为正
  std::size_t begin = 0;
  std::size_t end = 0;
};

}  // namespace tamias
