#include "ui/qt/text.h"

#include <QFont>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QRectF>
#include <QString>

namespace tac::qt {
namespace {

QString to_qstring(std::string_view text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

}  // namespace

FontMetrics QtTextMeasurer::measure(std::string_view utf8, const FontSpec& font) const {
  QFont qfont = QGuiApplication::instance() != nullptr ? QGuiApplication::font() : QFont();
  if (!font.family.empty()) {
    qfont.setFamily(to_qstring(font.family));
  }
  if (font.size_pt > 0.0F) {
    qfont.setPointSizeF(static_cast<double>(font.size_pt));
  }
  qfont.setBold(font.bold);
  qfont.setItalic(font.italic);

  const QFontMetricsF metrics(qfont);
  const QString text = to_qstring(utf8);
  const QRectF bounds = metrics.boundingRect(text);
  FontMetrics out;
  out.width = static_cast<float>(metrics.horizontalAdvance(text));
  out.height = static_cast<float>(bounds.height());
  out.ascent = static_cast<float>(metrics.ascent());
  out.descent = static_cast<float>(metrics.descent());
  return out;
}

}  // namespace tac::qt
