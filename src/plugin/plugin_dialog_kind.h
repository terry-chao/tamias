#pragma once

#include <cstdint>

namespace tamias {

enum class PluginDialogKind : std::int32_t {
  Message = 0,
  PromptString = 1,
  PromptNumber = 2,
  OpenFile = 3,
  SaveFile = 4,
  Form = 5,
};

}  // namespace tamias
