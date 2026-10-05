#pragma once

#include "ui/tac/platform/theme.h"

namespace tac::qt {

// Theme 的 Qt 实现。与 app/base/theme.cpp 的取色完全一致，阶段 1 迁移完成后
// app/base/theme.* 删除，这里成为唯一实现。
class QtTheme final : public Theme {
 public:
  [[nodiscard]] bool is_dark() const override;
  [[nodiscard]] ThemePalette palette() const override;
};

}  // namespace tac::qt
