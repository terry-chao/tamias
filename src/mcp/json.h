#pragma once

// 极简 JSON：MCP 门面只需要「解析任意 JSON-RPC 消息 / 序列化结果」这两件事，
// 所以不引第三方库（3rdparty 里也没有现成的）。范围有意识地窄：
//   - 值类型：null / bool / 整数 / 浮点 / 字符串 / 数组 / 对象
//   - 对象保留插入顺序（工具结果可读、测试可断言）
//   - 只做 UTF-8 文本，不做 BOM / 注释 / 尾逗号（那些只在手写配置里要好，
//     线上协议不该容忍）
//
// 刻意不做的事：数字精度的十进制往返（MCP 的参数是 id / 尺寸，双精度够用）、
// 流式解析、错误恢复。解析失败就整体失败——半个对象比没有更危险。

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace tamias::mcp {

class Json {
 public:
  enum class Type { kNull, kBool, kInt, kDouble, kString, kArray, kObject };

  Json() = default;

  static Json null() { return Json(); }
  static Json boolean(bool value);
  static Json integer(std::int64_t value);
  static Json number(double value);
  static Json string(std::string value);
  static Json array();
  static Json object();
  static Json object(std::initializer_list<std::pair<std::string, Json>> members);

  [[nodiscard]] Type type() const { return type_; }
  [[nodiscard]] bool is_null() const { return type_ == Type::kNull; }
  [[nodiscard]] bool is_bool() const { return type_ == Type::kBool; }
  [[nodiscard]] bool is_int() const { return type_ == Type::kInt; }
  [[nodiscard]] bool is_double() const { return type_ == Type::kDouble; }
  [[nodiscard]] bool is_number() const { return is_int() || is_double(); }
  [[nodiscard]] bool is_string() const { return type_ == Type::kString; }
  [[nodiscard]] bool is_array() const { return type_ == Type::kArray; }
  [[nodiscard]] bool is_object() const { return type_ == Type::kObject; }

  [[nodiscard]] bool as_bool() const { return bool_; }
  [[nodiscard]] std::int64_t as_int() const;
  [[nodiscard]] double as_double() const;
  [[nodiscard]] const std::string& as_string() const { return string_; }
  // 拿不到就当空：调用方在协议边界上本来也要处理字段缺失。
  [[nodiscard]] const std::vector<Json>& items() const { return array_; }
  [[nodiscard]] const std::vector<std::pair<std::string, Json>>& members() const {
    return object_;
  }

  // 对象成员访问。缺失 / 类型不符返回 nullptr，调用方自己判空。
  [[nodiscard]] const Json* find(std::string_view key) const;
  [[nodiscard]] Json* find(std::string_view key);
  [[nodiscard]] bool contains(std::string_view key) const { return find(key) != nullptr; }

  void set(std::string key, Json value);
  void push_back(Json value);
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] bool empty() const { return size() == 0; }

  // 解析。失败返回 nullopt 并把原因写进 error（若给了）。
  [[nodiscard]] static std::optional<Json> parse(std::string_view text,
                                                 std::string* error = nullptr);
  // 序列化。minified；字符串按 JSON 转义，非 ASCII 原样输出为 UTF-8。
  [[nodiscard]] std::string dump() const;

 private:
  Type type_ = Type::kNull;
  bool bool_ = false;
  std::int64_t int_ = 0;
  double double_ = 0.0;
  std::string string_;
  std::vector<Json> array_;
  std::vector<std::pair<std::string, Json>> object_;
};

}  // namespace tamias::mcp
