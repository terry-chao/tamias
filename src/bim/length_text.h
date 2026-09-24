#pragma once

#include <string>

namespace tamias {

// 长度/标高的显示文本。文档内部一律用米，显示按毫米精度（三位小数）——
// 和工程图习惯一致，标高和尺寸用同一套格式，读起来才不别扭。
//
// 以后要做「单位偏好」（mm / cm / 英尺）时改这两个函数就够了，别在 UI 里各写一份。

// 0 → "±0.000"；3.6 → "+3.600"；-1.2 → "-1.200"。
[[nodiscard]] std::string format_elevation(double meters);
// 6.0 → "6.000"；0.25 → "0.250"（不带正负号，尺寸没有 ± 的说法）。
[[nodiscard]] std::string format_distance(double meters);

}  // namespace tamias
