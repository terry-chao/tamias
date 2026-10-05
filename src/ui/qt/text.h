#pragma once

#include "ui/tac/platform/text.h"

namespace tac::qt {

// TextMeasurer 的 Qt 实现，基于 QFontMetricsF。
class QtTextMeasurer final : public TextMeasurer {
 public:
  [[nodiscard]] FontMetrics measure(std::string_view utf8, const FontSpec& font) const override;
};

}  // namespace tac::qt
