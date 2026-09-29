#pragma once

// tamias::math 模块试点用的对比探针。
//
// 同一段计算体（probe_body.inc）分别以“只看头文件”和“只 import 模块”两种方式
// 编译两次，然后逐项比对。两侧必须分处不同 TU：同一个 TU 里同时 include 头文件和
// import 模块，会让同名实体既挂在全局模块又挂在具名模块上。

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>

namespace tamias_math_probe {

inline constexpr std::size_t kProbeCount = 23;

struct HeaderTag {};
struct ModuleTag {};

std::array<float, kProbeCount> probe_from_headers();
std::array<float, kProbeCount> probe_from_module();

// 返回空字符串表示两侧一致；否则返回第一条不一致的描述。
std::string compare_probes();

}  // namespace tamias_math_probe
