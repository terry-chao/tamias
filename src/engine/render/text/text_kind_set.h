#pragma once

#include "engine/render/text/text_kind.h"

#include <array>
#include <cstddef>

namespace tamias {

// 文字类别的显示开关。视图层拿它决定「这一帧要不要画这类标注」。
// 默认全开：新加的类别不会因为忘了初始化而悄悄不显示。
class TextKindSet {
 public:
  TextKindSet() { visible_.fill(true); }

  [[nodiscard]] bool visible(TextKind kind) const {
    return visible_[static_cast<std::size_t>(kind)];
  }
  void set(TextKind kind, bool visible) { visible_[static_cast<std::size_t>(kind)] = visible; }
  void set_all(bool visible) { visible_.fill(visible); }
  void toggle(TextKind kind) { set(kind, !visible(kind)); }

 private:
  std::array<bool, kTextKindCount> visible_{};
};

}  // namespace tamias
