#include "host/command_echo.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace tamias {
namespace {

// 最短往返表示：0.2 就是 "0.2"，比 %.17g 的 "0.20000000000000001" 好读，
// 抄回去再解析仍是同一个值（std::to_chars 保证 round-trip）。
template <typename T>
[[nodiscard]] std::string number_literal(T value) {
  if (!std::isfinite(value)) {
    const bool is_float = sizeof(T) == sizeof(float);
    const char* prefix = is_float ? "float." : "double.";
    if (std::isnan(value)) {
      return std::string(prefix) + "NaN";
    }
    return std::string(prefix) + (value > 0 ? "PositiveInfinity" : "NegativeInfinity");
  }
  char buffer[64];
  const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
  if (ec != std::errc{}) {
    return "0";
  }
  std::string text(buffer, end);
  // C# 的浮点字面量要后缀，否则整数字面量会按 int / long 推。
  if constexpr (sizeof(T) == sizeof(float)) {
    text += 'f';
  }
  return text;
}

// C# 字符串字面量转义（够用即可：引号 / 反斜杠 / 空白）。
[[nodiscard]] std::string escape_csharp(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  for (const char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

// ABI 的点数组只带坐标（PickPoint 的 EntityId 不过文本协议），所以补 0。
[[nodiscard]] std::string pick_point_literal(Vec3 point) {
  return "new PickPoint(" + number_literal(point.x) + ", " + number_literal(point.y) + ", " +
         number_literal(point.z) + ", 0)";
}

[[nodiscard]] std::string point_list_literal(const std::vector<Vec3>& points) {
  std::string out = "[";
  for (std::size_t i = 0; i < points.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += pick_point_literal(points[i]);
  }
  out += "]";
  return out;
}

[[nodiscard]] std::string double_list_literal(const std::vector<double>& values) {
  std::string out = "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      out += ", ";
    }
    out += number_literal(values[i]);
  }
  out += "]";
  return out;
}

// 一个参数值 → `SetXxx("key", …)` 链上的一段。
[[nodiscard]] std::string arg_setter(const std::string& key, const CommandArg& value) {
  const std::string quoted = "\"" + escape_csharp(key) + "\"";
  return std::visit(
      [&quoted]<typename T>(const T& v) -> std::string {
        using V = std::decay_t<T>;
        if constexpr (std::is_same_v<V, double>) {
          return ".SetDouble(" + quoted + ", " + number_literal(v) + ")";
        } else if constexpr (std::is_same_v<V, std::int64_t>) {
          return ".SetInt(" + quoted + ", " + std::to_string(v) + ")";
        } else if constexpr (std::is_same_v<V, Vec3>) {
          return ".SetVec3(" + quoted + ", " + number_literal(v.x) + ", " +
                 number_literal(v.y) + ", " + number_literal(v.z) + ")";
        } else if constexpr (std::is_same_v<V, std::string>) {
          return ".SetString(" + quoted + ", \"" + escape_csharp(v) + "\")";
        } else if constexpr (std::is_same_v<V, std::vector<Vec3>>) {
          return ".SetPoints(" + quoted + ", " + point_list_literal(v) + ")";
        } else {
          return ".SetDoubles(" + quoted + ", " + double_list_literal(v) + ")";
        }
      },
      value);
}

}  // namespace

std::string format_dispatch_call(std::string_view name, const CommandArgs& args) {
  std::string out = "host.Dispatch(\"" + escape_csharp(name) + "\"";
  if (!args.empty()) {
    // CommandArgs 是无序表：按键排序，回显才是确定的。
    std::vector<const CommandArgs::value_type*> ordered;
    ordered.reserve(args.size());
    for (const auto& entry : args) {
      ordered.push_back(&entry);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const CommandArgs::value_type* a, const CommandArgs::value_type* b) {
                return a->first < b->first;
              });
    out += ", new CommandArgs()";
    for (const auto* entry : ordered) {
      out += arg_setter(entry->first, entry->second);
    }
  }
  out += ");";
  return out;
}

}  // namespace tamias
