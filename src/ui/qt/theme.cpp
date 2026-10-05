#include "ui/qt/theme.h"

#include <QApplication>
#include <QColor>
#include <QGuiApplication>
#include <QPalette>
#include <QStyleHints>

namespace tac::qt {
namespace {

Color from_qcolor(const QColor& color) {
  return Color::rgba(static_cast<std::uint8_t>(color.red()),
                     static_cast<std::uint8_t>(color.green()),
                     static_cast<std::uint8_t>(color.blue()),
                     static_cast<std::uint8_t>(color.alpha()));
}

}  // namespace

bool QtTheme::is_dark() const {
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

ThemePalette QtTheme::palette() const {
  ThemePalette p;
  if (is_dark()) {
    p.surface = from_qcolor(QColor(43, 45, 48));  // #2b2d30，和暗色工具条一致
    p.border = from_qcolor(QColor(60, 63, 65));   // #3c3f41
    p.divider = from_qcolor(QColor(255, 255, 255, 28));
    p.text = from_qcolor(QColor(220, 220, 220));
    p.text_muted = from_qcolor(QColor(130, 136, 145));
    p.icon = from_qcolor(QColor(216, 212, 206));
    p.hover = from_qcolor(QColor(255, 255, 255, 30));
    p.checked = from_qcolor(QColor(47, 125, 222, 115));
    p.accent = from_qcolor(QColor(111, 168, 240));
  } else {
    p.surface = from_qcolor(QColor(244, 245, 247));  // #f4f5f7，和浅色工具条一致
    p.border = from_qcolor(QColor(217, 220, 225));   // #d9dce1
    p.divider = from_qcolor(QColor(226, 229, 234));  // #e2e5ea
    p.text = from_qcolor(QColor(32, 33, 36));
    p.text_muted = from_qcolor(QColor(154, 160, 166));
    p.icon = from_qcolor(QColor(74, 79, 87));  // 浅底上用深图标
    p.hover = from_qcolor(QColor(0, 0, 0, 20));
    p.checked = from_qcolor(QColor(215, 230, 248));  // #d7e6f8
    p.accent = from_qcolor(QColor(26, 86, 184));
  }
  return p;
}

}  // namespace tac::qt
