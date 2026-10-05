#pragma once

#include "ui/tac/core/types.h"

#include <cstdint>
#include <string>

namespace tac {

// 具名键。可打印字符不放这里——走 KeyEvent::text，避免把键盘布局烘进枚举。
enum class Key : std::uint16_t {
  Unknown = 0,
  Escape,
  Enter,
  Tab,
  Backspace,
  Delete,
  Insert,
  Space,
  Left,
  Right,
  Up,
  Down,
  Home,
  End,
  PageUp,
  PageDown,
  F1,
  F2,
  F3,
  F4,
  F5,
  F6,
  F7,
  F8,
  F9,
  F10,
  F11,
  F12,
};

enum class Modifier : std::uint32_t {
  None = 0,
  Shift = 1U << 0,
  Ctrl = 1U << 1,
  Alt = 1U << 2,
  Meta = 1U << 3,  // Windows 键 / Command 键
};

[[nodiscard]] constexpr Modifier operator|(Modifier a, Modifier b) {
  return static_cast<Modifier>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

[[nodiscard]] constexpr bool has_modifier(Modifier set, Modifier bit) {
  return (static_cast<std::uint32_t>(set) & static_cast<std::uint32_t>(bit)) != 0;
}

enum class MouseButton : std::uint8_t {
  None = 0,
  Left,
  Middle,
  Right,
  X1,
  X2,
};

struct MouseEvent {
  MouseButton button = MouseButton::None;
  Point position{};
  Modifier modifiers = Modifier::None;
  bool pressed = false;
  int click_count = 1;
};

struct WheelEvent {
  // 正值 = 向上 / 向左滚。后端负责把平台习惯归一化到这里，壳不要再翻一次符号。
  Point delta{};
  Point position{};
  Modifier modifiers = Modifier::None;
};

struct KeyEvent {
  Key key = Key::Unknown;
  Modifier modifiers = Modifier::None;
  bool pressed = false;
  bool repeat = false;
  std::string text;  // 本次按键产生的 UTF-8 文本（无则空）
};

struct ResizeEvent {
  Size size{};
  float dpr = 1.0F;
};

}  // namespace tac
