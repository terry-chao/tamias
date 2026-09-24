#pragma once

#include <cstdint>
#include <string_view>

namespace tamias {

// 行内对齐。参考宽度取「折行宽度 max_width」；max_width 为 0（不折行）时
// 取自然宽度（最长那一行），所以单行右对齐会把整块推到参考宽度右侧。
enum class TextAlign : std::uint8_t {
  Left = 0,
  Center = 1,
  Right = 2,
};

// 反查（命令参数 / 序列化用）。认不出的一律左对齐。
[[nodiscard]] inline TextAlign text_align_from_name(std::string_view name) {
  if (name == "center") {
    return TextAlign::Center;
  }
  if (name == "right") {
    return TextAlign::Right;
  }
  return TextAlign::Left;
}

}  // namespace tamias
