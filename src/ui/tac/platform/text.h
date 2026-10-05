#pragma once

#include <string>
#include <string_view>

namespace tac {

struct FontSpec {
  std::string family;  // 空 = 界面默认字体
  float size_pt = 0.0F;
  bool bold = false;
  bool italic = false;
};

struct FontMetrics {
  float width = 0.0F;
  float height = 0.0F;
  float ascent = 0.0F;
  float descent = 0.0F;
};

class TextMeasurer {
 public:
  virtual ~TextMeasurer() = default;

  [[nodiscard]] virtual FontMetrics measure(std::string_view utf8, const FontSpec& font) const = 0;
};

}  // namespace tac
