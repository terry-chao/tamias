// C++20 模块试点：把 header-only 的 tamias::math 打包成模块 `tamias.math`。
//
// 扩展名用 .cppm：与 libc++（std.cppm）、Vulkan-Headers（vulkan.cppm）、GLM（glm.cppm）、
// Emscripten 自带测试同一惯例，clang/GCC 的驱动原生认；MSVC 的 cl 不认 .cppm，靠 CMake
// 传 /TP /interface 补上（实测可行）。取舍和驱动识别矩阵见 docs/DECISION-MODULES-MATH.md。
//
// 试点边界：
//   * 头文件仍是唯一事实来源，也是对外接口；本文件只做打包，不复制实现。
//   * 一个 TU 二选一 —— 要么 #include 头文件，要么 import tamias.math。
//     同一个 TU 里混用会让同名实体同时挂在全局模块和具名模块上（ODR 冲突），
//     所以两侧的对比测试分处两个 TU（见 tests/math_module/）。
//   * 试点承诺范围是 MSVC + Linux；Emscripten 在独立脚手架里也编过并发过测试，
//     但真实 wasm 目标还没进 CI（见 src/engine/math/CMakeLists.txt 的提示）。
//
// 标准库头放在 global module fragment，只把 tamias 自己的声明放进 export 块，
// 避免顺手把 <algorithm>/<cmath> 的名字一起导出。

module;

#include <algorithm>
#include <cmath>
#include <cstddef>

export module tamias.math;

export {
#include "engine/math/aabb2.h"
#include "engine/math/math.h"
#include "engine/math/grid.h"
#include "engine/math/camera.h"
}
