#include "host/command_arg_json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <system_error>

namespace tamias {
namespace {

[[nodiscard]] bool has_forbidden_separator(std::string_view text) {
  return text.find(';') != std::string_view::npos;
}

[[nodiscard]] std::string format_number(double value) {
  char buf[40];
  const auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
  if (ec == std::errc{}) {
    return std::string(buf, ptr);
  }
  std::snprintf(buf, sizeof(buf), "%.17g", value);
  return std::string(buf);
}

[[nodiscard]] double json_number(const mcp::Json& value) {
  return value.is_int() ? static_cast<double>(value.as_int()) : value.as_double();
}

// `{x,y,z}` → Vec3 文本 `x,y,z`。
[[nodiscard]] std::optional<std::string> vec3_text(const mcp::Json& value) {
  if (!value.is_object()) {
    return std::nullopt;
  }
  const mcp::Json* x = value.find("x");
  const mcp::Json* y = value.find("y");
  const mcp::Json* z = value.find("z");
  if (x == nullptr || y == nullptr || z == nullptr || !x->is_number() || !y->is_number() ||
      !z->is_number()) {
    return std::nullopt;
  }
  return format_number(json_number(*x)) + "," + format_number(json_number(*y)) + "," +
         format_number(json_number(*z));
}

// `[x,y,z]` 也当作一个点：模型有时会把点写成裸数组。
[[nodiscard]] std::optional<std::string> vec3_text_from_array(const mcp::Json& value) {
  if (!value.is_array() || value.size() != 3) {
    return std::nullopt;
  }
  for (const mcp::Json& item : value.items()) {
    if (!item.is_number()) {
      return std::nullopt;
    }
  }
  return format_number(json_number(value.items()[0])) + "," +
         format_number(json_number(value.items()[1])) + "," +
         format_number(json_number(value.items()[2]));
}

[[nodiscard]] Result<std::string> encode_value(std::string_view key, const mcp::Json& value) {
  const std::string prefix = std::string(key) + "=";

  if (value.is_bool()) {
    return "i:" + prefix + (value.as_bool() ? "1" : "0");
  }
  if (value.is_int()) {
    return "i:" + prefix + std::to_string(value.as_int());
  }
  if (value.is_double()) {
    return "d:" + prefix + format_number(value.as_double());
  }
  if (value.is_string()) {
    if (has_forbidden_separator(value.as_string())) {
      return Err("argument '" + std::string(key) +
                 "' contains ';' which cannot be carried by the command text protocol");
    }
    return "s:" + prefix + value.as_string();
  }
  if (value.is_object()) {
    std::optional<std::string> vec = vec3_text(value);
    if (!vec) {
      return Err("argument '" + std::string(key) +
                 "' is an object but not a point ({x,y,z})");
    }
    return "v:" + prefix + *vec;
  }
  if (value.is_array()) {
    if (value.empty()) {
      return Err("argument '" + std::string(key) + "' is an empty array");
    }
    // 点列：每个元素都是 {x,y,z} 或 [x,y,z]。
    bool all_points = true;
    std::string points;
    for (const mcp::Json& item : value.items()) {
      std::optional<std::string> point = vec3_text(item);
      if (!point) {
        point = vec3_text_from_array(item);
      }
      if (!point) {
        all_points = false;
        break;
      }
      if (!points.empty()) {
        points.push_back('|');
      }
      points += *point;
    }
    if (all_points) {
      return "p:" + prefix + points;
    }
    bool all_numbers = true;
    std::string numbers;
    for (const mcp::Json& item : value.items()) {
      if (!item.is_number()) {
        all_numbers = false;
        break;
      }
      if (!numbers.empty()) {
        numbers.push_back('|');
      }
      numbers += format_number(json_number(item));
    }
    if (all_numbers) {
      return "a:" + prefix + numbers;
    }
    return Err("argument '" + std::string(key) +
               "' is an array of mixed types; use points ([{x,y,z}]) or numbers");
  }
  return Err("argument '" + std::string(key) + "' is null; omit the key instead");
}

}  // namespace

Result<std::string> format_command_arg_text(const mcp::Json& args) {
  if (!args.is_object()) {
    return Err("command arguments must be a JSON object");
  }
  std::string text;
  for (const auto& [key, value] : args.members()) {
    if (key.empty() || key.find_first_of("=;") != std::string::npos) {
      return Err("invalid argument name '" + key + "'");
    }
    auto encoded = encode_value(key, value);
    if (!encoded) {
      return Err(encoded.error());
    }
    if (!text.empty()) {
      text.push_back(';');
    }
    text += *encoded;
  }
  return text;
}

}  // namespace tamias
