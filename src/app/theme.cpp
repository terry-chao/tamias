#include "theme.h"

#include <QApplication>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace tamias {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    switch (hints->colorScheme()) {
      case Qt::ColorScheme::Dark:
        return true;
      case Qt::ColorScheme::Light:
        return false;
      case Qt::ColorScheme::Unknown:
        break;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

ThemePalette theme_palette(bool dark) {
  ThemePalette p;
  if (dark) {
    p.surface = QColor(43, 45, 48);          // #2b2d30，和暗色工具条一致
    p.border = QColor(60, 63, 65);           // #3c3f41
    p.divider = QColor(255, 255, 255, 28);
    p.text = QColor(220, 220, 220);
    p.text_muted = QColor(130, 136, 145);
    p.icon = QColor(216, 212, 206);
    p.hover = QColor(255, 255, 255, 30);
    p.checked = QColor(47, 125, 222, 115);
    p.accent = QColor(111, 168, 240);
  } else {
    p.surface = QColor(244, 245, 247);       // #f4f5f7，和浅色工具条一致
    p.border = QColor(217, 220, 225);        // #d9dce1
    p.divider = QColor(226, 229, 234);       // #e2e5ea
    p.text = QColor(32, 33, 36);
    p.text_muted = QColor(154, 160, 166);
    p.icon = QColor(74, 79, 87);             // 浅底上用深图标
    p.hover = QColor(0, 0, 0, 20);
    p.checked = QColor(215, 230, 248);       // #d7e6f8
    p.accent = QColor(26, 86, 184);
  }
  return p;
}

ThemePalette theme_palette() { return theme_palette(is_dark_theme()); }

QString css_color(const QColor& color) {
  if (color.alpha() == 255) {
    return color.name(QColor::HexRgb);
  }
  return QStringLiteral("rgba(%1, %2, %3, %4)")
      .arg(color.red())
      .arg(color.green())
      .arg(color.blue())
      .arg(color.alpha());
}

}  // namespace tamias
