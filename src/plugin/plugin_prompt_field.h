#pragma once

#include "plugin/plugin_prompt_field_kind.h"

#include <string>

namespace tamias {

struct PluginPromptField {
  PluginPromptFieldKind kind = PluginPromptFieldKind::String;
  std::string id;
  std::string label;
  std::string text;
  double number = 0.0;
  double min = 0.0;
  double max = 0.0;
  bool has_range = false;
  bool flag = false;
};

}  // namespace tamias
