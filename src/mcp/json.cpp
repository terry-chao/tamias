#include "mcp/json.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <system_error>

namespace tamias::mcp {

namespace {

void append_utf8(std::string& out, std::uint32_t code_point) {
  if (code_point <= 0x7F) {
    out.push_back(static_cast<char>(code_point));
  } else if (code_point <= 0x7FF) {
    out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else if (code_point <= 0xFFFF) {
    out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
  }
}

void append_escaped(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char raw : text) {
    const auto ch = static_cast<unsigned char>(raw);
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
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
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (ch < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", ch);
          out += buf;
        } else {
          out.push_back(raw);  // UTF-8 字节原样穿过
        }
        break;
    }
  }
  out.push_back('"');
}

void append_number(std::string& out, double value) {
  if (!std::isfinite(value)) {
    out += "null";  // JSON 没有 NaN / Inf；写 null 比写出非法字面量强
    return;
  }
  // 最短往返：0.2 就写 0.2，而不是 0.20000000000000001。
  // 工具结果是给模型和人看的，17 位有效数字只是噪音。
  char shortest[40];
  const auto [ptr, ec] = std::to_chars(shortest, shortest + sizeof(shortest), value);
  if (ec == std::errc{}) {
    out.append(shortest, ptr);
    return;
  }
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%.17g", value);
  out += buf;
}

void dump_into(std::string& out, const Json& value);

void dump_into(std::string& out, const Json& value) {
  switch (value.type()) {
    case Json::Type::kNull:
      out += "null";
      break;
    case Json::Type::kBool:
      out += value.as_bool() ? "true" : "false";
      break;
    case Json::Type::kInt:
      out += std::to_string(value.as_int());
      break;
    case Json::Type::kDouble:
      append_number(out, value.as_double());
      break;
    case Json::Type::kString:
      append_escaped(out, value.as_string());
      break;
    case Json::Type::kArray: {
      out.push_back('[');
      bool first = true;
      for (const Json& item : value.items()) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        dump_into(out, item);
      }
      out.push_back(']');
      break;
    }
    case Json::Type::kObject: {
      out.push_back('{');
      bool first = true;
      for (const auto& [key, member] : value.members()) {
        if (!first) {
          out.push_back(',');
        }
        first = false;
        append_escaped(out, key);
        out.push_back(':');
        dump_into(out, member);
      }
      out.push_back('}');
      break;
    }
  }
}

class Parser {
 public:
  explicit Parser(std::string_view text) : text_(text) {}

  std::optional<Json> parse(std::string* error) {
    skip_whitespace();
    std::optional<Json> value = parse_value();
    if (!value) {
      if (error != nullptr) {
        *error = error_;
      }
      return std::nullopt;
    }
    skip_whitespace();
    if (pos_ != text_.size()) {
      fail("trailing characters after JSON value");
      if (error != nullptr) {
        *error = error_;
      }
      return std::nullopt;
    }
    return value;
  }

 private:
  bool fail(std::string_view message) {
    if (error_.empty()) {
      error_ = std::string(message) + " at offset " + std::to_string(pos_);
    }
    return false;
  }

  void skip_whitespace() {
    while (pos_ < text_.size()) {
      const char ch = text_[pos_];
      if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
        ++pos_;
      } else {
        break;
      }
    }
  }

  bool at_end() const { return pos_ >= text_.size(); }
  char peek() const { return text_[pos_]; }

  bool literal(std::string_view expected) {
    if (text_.compare(pos_, expected.size(), expected) != 0) {
      return fail("invalid literal");
    }
    pos_ += expected.size();
    return true;
  }

  std::optional<Json> parse_value() {
    if (at_end()) {
      fail("unexpected end of input");
      return std::nullopt;
    }
    switch (peek()) {
      case 'n':
        return literal("null") ? std::optional<Json>(Json::null()) : std::nullopt;
      case 't':
        return literal("true") ? std::optional<Json>(Json::boolean(true)) : std::nullopt;
      case 'f':
        return literal("false") ? std::optional<Json>(Json::boolean(false)) : std::nullopt;
      case '"': {
        std::string out;
        if (!parse_string(out)) {
          return std::nullopt;
        }
        return Json::string(std::move(out));
      }
      case '[':
        return parse_array();
      case '{':
        return parse_object();
      default:
        return parse_number();
    }
  }

  std::optional<Json> parse_array() {
    ++pos_;  // '['
    Json result = Json::array();
    skip_whitespace();
    if (!at_end() && peek() == ']') {
      ++pos_;
      return result;
    }
    while (true) {
      skip_whitespace();
      std::optional<Json> item = parse_value();
      if (!item) {
        return std::nullopt;
      }
      result.push_back(std::move(*item));
      skip_whitespace();
      if (at_end()) {
        fail("unterminated array");
        return std::nullopt;
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == ']') {
        ++pos_;
        return result;
      }
      fail("expected ',' or ']' in array");
      return std::nullopt;
    }
  }

  std::optional<Json> parse_object() {
    ++pos_;  // '{'
    Json result = Json::object();
    skip_whitespace();
    if (!at_end() && peek() == '}') {
      ++pos_;
      return result;
    }
    while (true) {
      skip_whitespace();
      if (at_end() || peek() != '"') {
        fail("expected string key in object");
        return std::nullopt;
      }
      std::string key;
      if (!parse_string(key)) {
        return std::nullopt;
      }
      skip_whitespace();
      if (at_end() || peek() != ':') {
        fail("expected ':' after object key");
        return std::nullopt;
      }
      ++pos_;
      skip_whitespace();
      std::optional<Json> value = parse_value();
      if (!value) {
        return std::nullopt;
      }
      result.set(std::move(key), std::move(*value));
      skip_whitespace();
      if (at_end()) {
        fail("unterminated object");
        return std::nullopt;
      }
      if (peek() == ',') {
        ++pos_;
        continue;
      }
      if (peek() == '}') {
        ++pos_;
        return result;
      }
      fail("expected ',' or '}' in object");
      return std::nullopt;
    }
  }

  bool parse_hex4(std::uint32_t& out) {
    if (pos_ + 4 > text_.size()) {
      return fail("truncated \\u escape");
    }
    std::uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
      const char ch = text_[pos_ + static_cast<std::size_t>(i)];
      value <<= 4;
      if (ch >= '0' && ch <= '9') {
        value |= static_cast<std::uint32_t>(ch - '0');
      } else if (ch >= 'a' && ch <= 'f') {
        value |= static_cast<std::uint32_t>(ch - 'a' + 10);
      } else if (ch >= 'A' && ch <= 'F') {
        value |= static_cast<std::uint32_t>(ch - 'A' + 10);
      } else {
        return fail("invalid hex digit in \\u escape");
      }
    }
    pos_ += 4;
    out = value;
    return true;
  }

  bool parse_string(std::string& out) {
    ++pos_;  // opening quote
    while (true) {
      if (at_end()) {
        return fail("unterminated string");
      }
      const char ch = text_[pos_++];
      if (ch == '"') {
        return true;
      }
      if (ch != '\\') {
        out.push_back(ch);
        continue;
      }
      if (at_end()) {
        return fail("unterminated escape");
      }
      const char esc = text_[pos_++];
      switch (esc) {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u': {
          std::uint32_t code = 0;
          if (!parse_hex4(code)) {
            return false;
          }
          if (code >= 0xD800 && code <= 0xDBFF) {
            // 代理对：高一半后面必须跟 \uDC00..\uDFFF
            if (pos_ + 1 < text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u') {
              pos_ += 2;
              std::uint32_t low = 0;
              if (!parse_hex4(low)) {
                return false;
              }
              if (low >= 0xDC00 && low <= 0xDFFF) {
                code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
              } else {
                return fail("invalid low surrogate");
              }
            } else {
              return fail("lone high surrogate");
            }
          }
          append_utf8(out, code);
          break;
        }
        default:
          return fail("invalid escape sequence");
      }
    }
  }

  std::optional<Json> parse_number() {
    const std::size_t start = pos_;
    if (!at_end() && (peek() == '-' || peek() == '+')) {
      ++pos_;
    }
    bool is_int = true;
    while (!at_end()) {
      const char ch = peek();
      if (ch >= '0' && ch <= '9') {
        ++pos_;
      } else if (ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-') {
        is_int = false;
        ++pos_;
      } else {
        break;
      }
    }
    if (pos_ == start) {
      fail("invalid number");
      return std::nullopt;
    }
    const std::string text(text_.substr(start, pos_ - start));
    if (is_int) {
      std::size_t consumed = 0;
      try {
        const long long value = std::stoll(text, &consumed);
        if (consumed == text.size()) {
          return Json::integer(static_cast<std::int64_t>(value));
        }
      } catch (const std::exception&) {
        // 溢出就退回 double，交给下面处理。
      }
    }
    try {
      const double value = std::stod(text);
      return Json::number(value);
    } catch (const std::exception&) {
      fail("invalid number");
      return std::nullopt;
    }
  }

  std::string_view text_;
  std::size_t pos_ = 0;
  std::string error_;
};

}  // namespace

Json Json::boolean(bool value) {
  Json json;
  json.type_ = Type::kBool;
  json.bool_ = value;
  return json;
}

Json Json::integer(std::int64_t value) {
  Json json;
  json.type_ = Type::kInt;
  json.int_ = value;
  return json;
}

Json Json::number(double value) {
  Json json;
  json.type_ = Type::kDouble;
  json.double_ = value;
  return json;
}

Json Json::string(std::string value) {
  Json json;
  json.type_ = Type::kString;
  json.string_ = std::move(value);
  return json;
}

Json Json::array() {
  Json json;
  json.type_ = Type::kArray;
  return json;
}

Json Json::object() {
  Json json;
  json.type_ = Type::kObject;
  return json;
}

Json Json::object(std::initializer_list<std::pair<std::string, Json>> members) {
  Json json = object();
  for (const auto& member : members) {
    json.set(member.first, member.second);
  }
  return json;
}

std::int64_t Json::as_int() const {
  if (type_ == Type::kInt) {
    return int_;
  }
  if (type_ == Type::kDouble) {
    return static_cast<std::int64_t>(double_);
  }
  return 0;
}

double Json::as_double() const {
  if (type_ == Type::kDouble) {
    return double_;
  }
  if (type_ == Type::kInt) {
    return static_cast<double>(int_);
  }
  return 0.0;
}

const Json* Json::find(std::string_view key) const {
  if (type_ != Type::kObject) {
    return nullptr;
  }
  for (const auto& [name, value] : object_) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

Json* Json::find(std::string_view key) {
  if (type_ != Type::kObject) {
    return nullptr;
  }
  for (auto& [name, value] : object_) {
    if (name == key) {
      return &value;
    }
  }
  return nullptr;
}

void Json::set(std::string key, Json value) {
  if (type_ != Type::kObject) {
    type_ = Type::kObject;
    object_.clear();
  }
  for (auto& [name, existing] : object_) {
    if (name == key) {
      existing = std::move(value);
      return;
    }
  }
  object_.emplace_back(std::move(key), std::move(value));
}

void Json::push_back(Json value) {
  if (type_ != Type::kArray) {
    type_ = Type::kArray;
    array_.clear();
  }
  array_.push_back(std::move(value));
}

std::size_t Json::size() const {
  switch (type_) {
    case Type::kArray:
      return array_.size();
    case Type::kObject:
      return object_.size();
    default:
      return 0;
  }
}

std::optional<Json> Json::parse(std::string_view text, std::string* error) {
  Parser parser(text);
  return parser.parse(error);
}

std::string Json::dump() const {
  std::string out;
  dump_into(out, *this);
  return out;
}

}  // namespace tamias::mcp
