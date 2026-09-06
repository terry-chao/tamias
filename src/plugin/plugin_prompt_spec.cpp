#include "plugin/plugin_prompt_spec.h"

#include <charconv>
#include <sstream>
#include <system_error>
#include <utility>
#include <vector>

namespace tamias {
namespace {

[[nodiscard]] std::string unescape(std::string_view text) {
  std::string out(text);
  for (char& c : out) {
    if (static_cast<unsigned char>(c) == 0x1e) {
      c = '\n';
    }
  }
  return out;
}

[[nodiscard]] std::vector<std::string_view> split_lines(std::string_view text) {
  std::vector<std::string_view> lines;
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const auto end = text.find('\n', begin);
    const auto n = end == std::string_view::npos ? text.size() : end;
    auto line = text.substr(begin, n - begin);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (!line.empty()) {
      lines.push_back(line);
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return lines;
}

[[nodiscard]] std::vector<std::string> split_pipe(std::string_view line) {
  std::vector<std::string> parts;
  std::size_t begin = 0;
  while (begin <= line.size()) {
    const auto end = line.find('|', begin);
    const auto n = end == std::string_view::npos ? line.size() : end;
    parts.emplace_back(line.substr(begin, n - begin));
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return parts;
}

[[nodiscard]] Result<double> parse_double(std::string_view text) {
  double value = 0.0;
  const auto* first = text.data();
  const auto* last = first + text.size();
  auto parsed = std::from_chars(first, last, value);
  if (parsed.ec != std::errc{} || parsed.ptr != last) {
    return Err("invalid number '" + std::string(text) + "'");
  }
  return value;
}

[[nodiscard]] Result<void> apply_field_line(PluginPromptSpec& spec, const std::vector<std::string>& parts) {
  if (parts.size() < 5) {
    return Err("form field needs kind, id, label, and value");
  }
  PluginPromptField field;
  field.id = parts[2];
  field.label = unescape(parts[3]);
  if (field.id.empty()) {
    return Err("form field id is empty");
  }
  const auto& kind = parts[1];
  if (kind == "n") {
    field.kind = PluginPromptFieldKind::Number;
    auto number = parse_double(parts[4]);
    if (!number) {
      return Err(number.error());
    }
    field.number = *number;
    if (parts.size() >= 7) {
      auto min = parse_double(parts[5]);
      auto max = parse_double(parts[6]);
      if (!min) {
        return Err(min.error());
      }
      if (!max) {
        return Err(max.error());
      }
      field.min = *min;
      field.max = *max;
      field.has_range = true;
    }
  } else if (kind == "b") {
    field.kind = PluginPromptFieldKind::Bool;
    field.flag = parts[4] == "1" || parts[4] == "true" || parts[4] == "True";
  } else if (kind == "s") {
    field.kind = PluginPromptFieldKind::String;
    field.text = unescape(parts[4]);
  } else {
    return Err("unknown form field kind '" + kind + "'");
  }
  spec.fields.push_back(std::move(field));
  return {};
}

}  // namespace

Result<PluginPromptSpec> parse_plugin_prompt_spec(std::string_view text) {
  PluginPromptSpec spec;
  for (const auto line : split_lines(text)) {
    const auto parts = split_pipe(line);
    if (parts.empty()) {
      continue;
    }
    const auto& tag = parts[0];
    if (tag == "T" && parts.size() >= 2) {
      spec.title = unescape(parts[1]);
    } else if (tag == "L" && parts.size() >= 2) {
      spec.label = unescape(parts[1]);
    } else if (tag == "V" && parts.size() >= 2) {
      spec.value = unescape(parts[1]);
      if (auto number = parse_double(parts[1]); number) {
        spec.number = *number;
      }
    } else if (tag == "MIN" && parts.size() >= 2) {
      auto min = parse_double(parts[1]);
      if (!min) {
        return Err(min.error());
      }
      spec.min = *min;
      spec.has_min = true;
    } else if (tag == "MAX" && parts.size() >= 2) {
      auto max = parse_double(parts[1]);
      if (!max) {
        return Err(max.error());
      }
      spec.max = *max;
      spec.has_max = true;
    } else if (tag == "FILTER" && parts.size() >= 2) {
      spec.filter = parts[1];
    } else if (tag == "NAME" && parts.size() >= 2) {
      spec.default_name = parts[1];
    } else if (tag == "F") {
      if (auto r = apply_field_line(spec, parts); !r) {
        return Err(r.error());
      }
    } else {
      return Err("unknown prompt spec line '" + std::string(line) + "'");
    }
  }
  return spec;
}

std::string serialize_plugin_form_values(const PluginPromptSpec& spec) {
  std::ostringstream out;
  bool first = true;
  for (const auto& field : spec.fields) {
    if (!first) {
      out << ';';
    }
    first = false;
    switch (field.kind) {
      case PluginPromptFieldKind::Number:
        out << "n:" << field.id << '=' << field.number;
        break;
      case PluginPromptFieldKind::Bool:
        out << "b:" << field.id << '=' << (field.flag ? '1' : '0');
        break;
      case PluginPromptFieldKind::String:
        out << "s:" << field.id << '=' << field.text;
        break;
    }
  }
  return out.str();
}

Result<void> apply_plugin_form_values(PluginPromptSpec& spec, std::string_view text) {
  std::size_t begin = 0;
  while (begin <= text.size()) {
    const auto end = text.find(';', begin);
    const auto n = end == std::string_view::npos ? text.size() : end;
    auto token = text.substr(begin, n - begin);
    if (!token.empty()) {
      const auto colon = token.find(':');
      const auto eq = token.find('=');
      if (colon == std::string_view::npos || eq == std::string_view::npos || eq <= colon + 1) {
        return Err("invalid form value '" + std::string(token) + "'");
      }
      const auto id = std::string(token.substr(colon + 1, eq - colon - 1));
      const auto value = token.substr(eq + 1);
      PluginPromptField* field = nullptr;
      for (auto& candidate : spec.fields) {
        if (candidate.id == id) {
          field = &candidate;
          break;
        }
      }
      if (field == nullptr) {
        return Err("unknown form field '" + id + "'");
      }
      switch (field->kind) {
        case PluginPromptFieldKind::Number: {
          auto number = parse_double(value);
          if (!number) {
            return Err(number.error());
          }
          field->number = *number;
          break;
        }
        case PluginPromptFieldKind::Bool:
          field->flag = value == "1" || value == "true" || value == "True";
          break;
        case PluginPromptFieldKind::String:
          field->text = std::string(value);
          break;
      }
    }
    if (end == std::string_view::npos) {
      break;
    }
    begin = end + 1;
  }
  return {};
}

}  // namespace tamias
