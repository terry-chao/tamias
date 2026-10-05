#pragma once

#include "ui/tac/core/types.h"

namespace tac {

enum class ColorScheme {
  Light,
  Dark,
};

// 视口周边部件（工具列、构件面板）用的一套界面色。与 app/base/theme.h 的
// ThemePalette 一一对应；迁移完成后 app/base/theme.* 并入 src/ui/qt。
struct ThemePalette {
  Color surface;     // 面板底色
  Color border;      // 与画布 / 停靠区之间的分界线
  Color divider;     // 面板内部的分区线
  Color text;        // 主文字
  Color text_muted;  // 次要文字
  Color icon;        // 单色图标着色
  Color hover;       // 悬停底色
  Color checked;     // 选中 / 激活底色
  Color accent;      // 强调文字
};

class Theme {
 public:
  virtual ~Theme() = default;

  [[nodiscard]] virtual bool is_dark() const = 0;
  [[nodiscard]] virtual ColorScheme scheme() const {
    return is_dark() ? ColorScheme::Dark : ColorScheme::Light;
  }
  [[nodiscard]] virtual ThemePalette palette() const = 0;
};

}  // namespace tac
