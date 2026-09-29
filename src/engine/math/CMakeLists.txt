add_library(tamias_math INTERFACE)
add_library(tamias::math ALIAS tamias_math)
target_include_directories(tamias_math INTERFACE ${CMAKE_SOURCE_DIR}/src)

# C++20 模块试点（默认关）。tamias::math 体量小、依赖少、纯头文件，用它验证
# “模块能不能在三平台工具链 + CMake 上稳定构建”，而不是现在就替换头文件。
#
# 打开后多出一个 tamias_math_module target：把同名头文件包成模块 tamias.math，
# 供 tamias::math_module 的消费者 import。头文件路径不受影响。
option(TAMIAS_ENABLE_MATH_MODULE "Build the tamias.math C++20 module (pilot; MSVC + Linux verified)." OFF)

if(TAMIAS_ENABLE_MATH_MODULE)
  if(CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR
      "TAMIAS_ENABLE_MATH_MODULE requires CMake 3.28+ (FILE_SET CXX_MODULES); "
      "found ${CMAKE_VERSION}.")
  endif()
  if(EMSCRIPTEN)
    # 脚手架里 Emscripten 也编过了，但真实 wasm 目标还没进 CI。另外 emcc 的模块扫描
    # 依赖已装好的 sysroot：EM_CACHE 全新时 `emscan-deps` 可能报 'cstdio' not found，
    # 先跑一次普通 emcc 编译预热即可。默认仍是关的，要用再显式打开。
    message(STATUS
      "TAMIAS_ENABLE_MATH_MODULE: Emscripten 路径尚未进 CI，注意冷 EM_CACHE 的扫描失败。")
  endif()

  add_library(tamias_math_module)
  add_library(tamias::math_module ALIAS tamias_math_module)
  # 扩展名用 .cppm（不是 .ixx）：跨平台侧的例子和实物都用它——libc++ 的 std.cppm、
  # Vulkan-Headers 的 vulkan.cppm、GLM 的 glm.cppm。MSVC 的 cl 不认 .cppm，靠 CMake
  # 传 /TP /interface 补上；clang/GCC 原生认。识别矩阵见 docs/DECISION-MODULES-MATH.md。
  target_sources(tamias_math_module
    PUBLIC
      FILE_SET CXX_MODULES FILES ${CMAKE_CURRENT_SOURCE_DIR}/tamias.math.cppm
  )
  target_include_directories(tamias_math_module PUBLIC ${CMAKE_SOURCE_DIR}/src)
endif()
