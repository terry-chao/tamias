#pragma once

#include "engine/core/result.h"
#include "plugin/plugin_prompt_field.h"

#include <string>
#include <string_view>
#include <vector>

namespace tamias {

struct PluginPromptSpec {
  std::string title;
  std::string label;
  std::string value;
  std::string filter;
  std::string default_name;
  double number = 0.0;
  double min = 0.0;
  double max = 0.0;
  bool has_min = false;
  bool has_max = false;
  std::vector<PluginPromptField> fields;
};

[[nodiscard]] Result<PluginPromptSpec> parse_plugin_prompt_spec(std::string_view text);
[[nodiscard]] std::string serialize_plugin_form_values(const PluginPromptSpec& spec);
[[nodiscard]] Result<void> apply_plugin_form_values(PluginPromptSpec& spec, std::string_view text);

}  // namespace tamias
