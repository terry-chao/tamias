#include "host/command_arg_text.h"

#include <charconv>
#include <cstdio>
#include <string>
#include <system_error>

namespace tamias {
namespace {

[[nodiscard]] std::string_view trim(std::string_view s) {
  const auto begin = s.find_first_not_of(" \t");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = s.find_last_not_of(" \t");
  return s.substr(begin, end - begin + 1);
}

[[nodiscard]] Result<std::int64_t> parse_i64(std::string_view s) {
  std::int64_t value = 0;
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(first, last, value);
  if (ec != std::errc{} || ptr != last) {
    return Err("invalid integer '" + std::string(s) + "'");
  }
  return value;
}

[[nodiscard]] Result<double> parse_f64(std::string_view s) {
  // from_chars<double> is enough on MSVC / libstdc++; keep a sscanf fallback.
  double value = 0;
  const auto* first = s.data();
  const auto* last = s.data() + s.size();
  const auto [ptr, ec] = std::from_chars(first, last, value);
  if (ec == std::errc{} && ptr == last) {
    return value;
  }
  std::string owned(s);
  char extra = '\0';
  if (std::sscanf(owned.c_str(), "%lf%c", &value, &extra) == 1) {
    return value;
  }
  return Err("invalid number '" + owned + "'");
}

[[nodiscard]] Result<Vec3> parse_vec3(std::string_view s) {
  const auto c1 = s.find(',');
  const auto c2 = c1 == std::string_view::npos ? std::string_view::npos : s.find(',', c1 + 1);
  if (c1 == std::string_view::npos || c2 == std::string_view::npos ||
      s.find(',', c2 + 1) != std::string_view::npos) {
    return Err("vec3 needs three comma-separated numbers");
  }
  auto x = parse_f64(trim(s.substr(0, c1)));
  auto y = parse_f64(trim(s.substr(c1 + 1, c2 - c1 - 1)));
  auto z = parse_f64(trim(s.substr(c2 + 1)));
  if (!x) {
    return Err(x.error());
  }
  if (!y) {
    return Err(y.error());
  }
  if (!z) {
    return Err(z.error());
  }
  return Vec3{static_cast<float>(*x), static_cast<float>(*y), static_cast<float>(*z)};
}

[[nodiscard]] bool looks_like_int(std::string_view s) {
  if (s.empty()) {
    return false;
  }
  std::size_t i = (s[0] == '+' || s[0] == '-') ? 1 : 0;
  if (i >= s.size()) {
    return false;
  }
  for (; i < s.size(); ++i) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Result<CommandArg> infer_value(std::string_view raw) {
  if (raw.find(',') != std::string_view::npos) {
    auto v = parse_vec3(raw);
    if (!v) {
      return Err(v.error());
    }
    return CommandArg{*v};
  }
  if (looks_like_int(raw)) {
    auto n = parse_i64(raw);
    if (!n) {
      return Err(n.error());
    }
    return CommandArg{*n};
  }
  if (!raw.empty() && (raw[0] == '+' || raw[0] == '-' || (raw[0] >= '0' && raw[0] <= '9'))) {
    auto n = parse_f64(raw);
    if (n) {
      return CommandArg{*n};
    }
  }
  return CommandArg{std::string(raw)};
}

}  // namespace

Result<CommandArgs> parse_command_arg_text(std::string_view text) {
  CommandArgs out;
  std::string_view rest = trim(text);
  while (!rest.empty()) {
    const auto split = rest.find(';');
    const auto part = trim(split == std::string_view::npos ? rest : rest.substr(0, split));
    rest = split == std::string_view::npos ? std::string_view{} : rest.substr(split + 1);
    if (part.empty()) {
      continue;
    }
    const auto eq = part.find('=');
    if (eq == std::string_view::npos) {
      return Err("plugin arg missing '=': " + std::string(part));
    }
    auto key = trim(part.substr(0, eq));
    const auto value = trim(part.substr(eq + 1));
    if (key.empty()) {
      return Err("plugin arg has empty key");
    }

    char type = 0;
    if (key.size() >= 2 && key[1] == ':') {
      type = key[0];
      key = trim(key.substr(2));
      if (key.empty()) {
        return Err("plugin arg has empty key");
      }
    }

    Result<CommandArg> parsed = Err("internal");
    switch (type) {
      case 'i': {
        auto n = parse_i64(value);
        parsed = n ? Result<CommandArg>{CommandArg{*n}} : Result<CommandArg>{Err(n.error())};
        break;
      }
      case 'd': {
        auto n = parse_f64(value);
        parsed = n ? Result<CommandArg>{CommandArg{*n}} : Result<CommandArg>{Err(n.error())};
        break;
      }
      case 's':
        parsed = CommandArg{std::string(value)};
        break;
      case 'v': {
        auto v = parse_vec3(value);
        parsed = v ? Result<CommandArg>{CommandArg{*v}} : Result<CommandArg>{Err(v.error())};
        break;
      }
      case 0:
        parsed = infer_value(value);
        break;
      default:
        return Err("unknown plugin arg type '" + std::string(1, type) + "'");
    }
    if (!parsed) {
      return Err(parsed.error());
    }
    out.emplace(std::string(key), std::move(*parsed));
  }
  return out;
}

}  // namespace tamias
