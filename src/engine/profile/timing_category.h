#pragma once

#include <cstdint>
#include <string_view>

namespace tamias {

enum class TimingCategory : std::uint8_t { Command = 0, Modeling = 1, Render = 2, Ui = 3 };

inline constexpr int kTimingCategoryCount = 4;

[[nodiscard]] inline std::string_view timing_category_name(TimingCategory category) {
  switch (category) {
    case TimingCategory::Command:
      return "command";
    case TimingCategory::Modeling:
      return "modeling";
    case TimingCategory::Render:
      return "render";
    case TimingCategory::Ui:
      return "ui";
  }
  return "unknown";
}

}  // namespace tamias
