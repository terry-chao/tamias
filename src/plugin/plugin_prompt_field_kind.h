#pragma once

#include <cstdint>

namespace tamias {

enum class PluginPromptFieldKind : std::int32_t {
  String = 0,
  Number = 1,
  Bool = 2,
};

}  // namespace tamias
