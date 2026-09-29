#pragma once

#include "engine/math/math.h"

#include <cmath>
#include <cstdint>

namespace tamias {

// 交互层的输入词汇。这里刻意**不** include 任何壳 / Qt / 平台头：
// 壳负责把 QMouseEvent / 平台消息翻译成下面这些结构，反向不成立。

enum class PointerKind : std::uint8_t { Mouse, Pen, Touch };

enum class ButtonId : std::uint8_t { None, Primary, Secondary, Middle, Aux1, Aux2 };

// 指针事件的相位。Cancel = 设备丢失 / 被系统打断，不是用户抬起。
enum class PointerPhase : std::uint8_t { Down, Move, Up, Cancel };

// Down / Move 期间可能同时按着多个键。
struct ButtonMask {
  std::uint8_t bits = 0;

  static constexpr std::uint8_t bit_of(ButtonId button) {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(button));
  }
  [[nodiscard]] constexpr bool has(ButtonId button) const {
    return (bits & bit_of(button)) != 0;
  }
  constexpr void add(ButtonId button) { bits |= bit_of(button); }
  constexpr void remove(ButtonId button) { bits &= static_cast<std::uint8_t>(~bit_of(button)); }
};

struct Modifiers {
  bool shift = false;
  bool ctrl = false;
  bool alt = false;
  bool meta = false;
};

// 坐标约定：**逻辑像素 + 控件局部坐标**。DPR 由 DragContext 提供，
// 不混进事件里——否则高 DPI 下拾取容差和阈值都会算错。
struct PointerEvent {
  PointerPhase phase = PointerPhase::Move;
  ButtonId button = ButtonId::None;  // Down / Up 时有效
  ButtonMask buttons{};              // 事件发生时按住的键
  Vec2 pos{};
  Vec2 delta{};                      // Move 相对上一次
  Modifiers mods{};
  PointerKind kind = PointerKind::Mouse;
  std::uint32_t pointer_id = 0;      // 多指 / 触控笔
  float pressure = 0.f;              // 先接住不用，将来笔压有地方放
  double time_seconds = 0.0;         // 时间从事件里来，不从壳的计时器取
};

struct WheelEvent {
  Vec2 pos{};
  Vec2 delta{};
  Modifiers mods{};
  bool precise = false;  // 触控板 / 高精度滚轮
  double time_seconds = 0.0;
};

// 只映射交互层真正用到的键，不照抄 Qt 的 Key_*（否则交互层反向绑回 Qt 语义）。
enum class KeyCode : std::uint8_t {
  Unknown,
  Escape,
  Enter,
  Backspace,
  Delete,
  Tab,
  Space,
  Left,
  Right,
  Up,
  Down,
  BracketLeft,
  BracketRight,
};

struct KeyEvent {
  KeyCode code = KeyCode::Unknown;
  Modifiers mods{};
  bool down = true;
  bool repeat = false;
  double time_seconds = 0.0;
};

[[nodiscard]] inline float pointer_distance(Vec2 a, Vec2 b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace tamias
