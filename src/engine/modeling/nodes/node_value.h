#pragma once

#include "engine/math/math.h"
#include "engine/modeling/mki/kernel.h"

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

namespace tamias {

// 节点端口的类型。Body 泛指内核体（实体 / 轮廓面），由节点类型自行解释；
// 曲线（线框）P1 还没有，届时加 Wire。
enum class PortType : std::uint8_t {
  Empty = 0,
  Number,
  Integer,
  Bool,
  Text,
  Vector,
  Body,
};

[[nodiscard]] const char* to_string(PortType type);

// 端口匹配的强度：连线校验、端口变暗、搜索排序共用这一个刻度。
//   None     不能连；
//   Convert  能连但要隐式转换（Number ↔ Integer）；
//   Type     类型正好；
//   Exact    预留给「同类型同语义」（P1 等同 Type）。
enum class PortMatch : std::uint8_t { None = 0, Convert = 1, Type = 2, Exact = 3 };

[[nodiscard]] PortMatch port_match(PortType from, PortType to);
[[nodiscard]] inline bool can_convert(PortType from, PortType to) {
  return port_match(from, to) != PortMatch::None;
}

// 端口 / 参数的值。几何用 MKI 的不可变共享体（BodyRef），跨节点共享零拷贝，
// 后台求值（P2）整图克隆时也只拷指针。
class NodeValue {
 public:
  NodeValue() = default;

  static NodeValue empty() { return NodeValue(); }
  static NodeValue number(double v) {
    NodeValue out;
    out.value_ = v;
    return out;
  }
  static NodeValue integer(std::int64_t v) {
    NodeValue out;
    out.value_ = v;
    return out;
  }
  static NodeValue boolean(bool v) {
    NodeValue out;
    out.value_ = v;
    return out;
  }
  static NodeValue text(std::string v) {
    NodeValue out;
    out.value_ = std::move(v);
    return out;
  }
  static NodeValue vector(Vec3 v) {
    NodeValue out;
    out.value_ = v;
    return out;
  }
  static NodeValue body(BodyRef v) {
    NodeValue out;
    out.value_ = std::move(v);
    return out;
  }

  [[nodiscard]] PortType type() const {
    switch (value_.index()) {
      case 1:
        return PortType::Number;
      case 2:
        return PortType::Integer;
      case 3:
        return PortType::Bool;
      case 4:
        return PortType::Text;
      case 5:
        return PortType::Vector;
      case 6:
        return PortType::Body;
      default:
        return PortType::Empty;
    }
  }

  [[nodiscard]] bool is_empty() const { return value_.index() == 0; }

  // —— 取值（带类型转换 + 兜底；几何取不到就回 nullptr）——
  [[nodiscard]] double as_number(double fallback = 0.0) const {
    if (const double* v = std::get_if<double>(&value_)) {
      return *v;
    }
    if (const std::int64_t* v = std::get_if<std::int64_t>(&value_)) {
      return static_cast<double>(*v);
    }
    if (const bool* v = std::get_if<bool>(&value_)) {
      return *v ? 1.0 : 0.0;
    }
    return fallback;
  }
  [[nodiscard]] std::int64_t as_integer(std::int64_t fallback = 0) const {
    if (const std::int64_t* v = std::get_if<std::int64_t>(&value_)) {
      return *v;
    }
    if (const double* v = std::get_if<double>(&value_)) {
      return static_cast<std::int64_t>(*v);
    }
    return fallback;
  }
  [[nodiscard]] bool as_bool(bool fallback = false) const {
    if (const bool* v = std::get_if<bool>(&value_)) {
      return *v;
    }
    if (const double* v = std::get_if<double>(&value_)) {
      return *v != 0.0;
    }
    return fallback;
  }
  [[nodiscard]] const std::string& as_text() const {
    static const std::string kEmpty;
    if (const std::string* v = std::get_if<std::string>(&value_)) {
      return *v;
    }
    return kEmpty;
  }
  [[nodiscard]] Vec3 as_vector(Vec3 fallback = {}) const {
    if (const Vec3* v = std::get_if<Vec3>(&value_)) {
      return *v;
    }
    return fallback;
  }
  [[nodiscard]] BodyRef as_body() const {
    if (const BodyRef* v = std::get_if<BodyRef>(&value_)) {
      return *v;
    }
    return nullptr;
  }

  // 按目标端口类型转换（can_convert 放行的前提下）。转不动回 Empty。
  [[nodiscard]] NodeValue convert_to(PortType to) const;

  [[nodiscard]] bool operator==(const NodeValue& other) const {
    // Body 按指针同一性比较（体没有值语义）；Vec3 没有值相等运算符，手写；
    // 其余标量按值比较。逐分支 get_if，避免实例化 variant 的 operator==
    // （它要求所有可选类型都有 ==）。
    if (value_.index() != other.value_.index()) {
      return false;
    }
    if (const BodyRef* a = std::get_if<BodyRef>(&value_)) {
      return *a == *std::get_if<BodyRef>(&other.value_);
    }
    if (const Vec3* a = std::get_if<Vec3>(&value_)) {
      const Vec3* b = std::get_if<Vec3>(&other.value_);
      return a->x == b->x && a->y == b->y && a->z == b->z;
    }
    if (const double* a = std::get_if<double>(&value_)) {
      return *a == *std::get_if<double>(&other.value_);
    }
    if (const std::int64_t* a = std::get_if<std::int64_t>(&value_)) {
      return *a == *std::get_if<std::int64_t>(&other.value_);
    }
    if (const bool* a = std::get_if<bool>(&value_)) {
      return *a == *std::get_if<bool>(&other.value_);
    }
    if (const std::string* a = std::get_if<std::string>(&value_)) {
      return *a == *std::get_if<std::string>(&other.value_);
    }
    return true;  // monostate == monostate
  }

 private:
  std::variant<std::monostate, double, std::int64_t, bool, std::string, Vec3, BodyRef> value_;
};

}  // namespace tamias
