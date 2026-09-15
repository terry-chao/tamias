#pragma once

#include <QColor>

namespace tamias {

// 是否深色主题（跟随系统）。
[[nodiscard]] bool is_dark_theme();

// 视口周边部件（工具列、构件面板）用的一套界面色。
// 这些部件夹在三维画布和停靠面板之间：既不该用画布色，也不该写死黑色，
// 所以统一从系统深浅色推出一套可读性够用的配色。
struct ThemePalette {
  QColor surface;     // 面板底色
  QColor border;      // 与画布/停靠区之间的分界线
  QColor divider;     // 面板内部的分区线
  QColor text;        // 主文字
  QColor text_muted;  // 次要文字（计数、被隐藏的行）
  QColor icon;        // 图标着色（单色图标靠它上色）
  QColor hover;       // 悬停底色
  QColor checked;     // 选中/激活底色
  QColor accent;      // 强调文字
};

[[nodiscard]] ThemePalette theme_palette(bool dark);
[[nodiscard]] ThemePalette theme_palette();

// QSS 里用的颜色写法：带透明度时输出 rgba()。
[[nodiscard]] QString css_color(const QColor& color);

}  // namespace tamias
