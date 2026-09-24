#pragma once

#include <string>

namespace tamias {

// 字体引用（不是字体本身）。解析成真字体在 engine/render/text 的字体模块里做：
// family 先从 assets/fonts/ 找，找不到回落到系统字体，再找不到用默认字体。
// family 为空 = 默认字体。
struct FontRef {
  std::string family;
  float weight = 400.f;  // 400 = regular，700 = bold
  bool italic = false;
};

}  // namespace tamias
