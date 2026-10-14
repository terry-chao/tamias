cmake_minimum_required(VERSION 3.24)
# 本机 VC 只有 2052\clui.dll，VSLANG=1033 不会变成英文 "Note: including file:"。
if(WIN32)
  set(ENV{VSLANG} "1033")
endif()
project(tamias VERSION 0.1.0 LANGUAGES C CXX)

if(NOT EMSCRIPTEN AND NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Tamias currently supports Windows, Linux, and Emscripten.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_options(/utf-8 /MP /FS /W4 /permissive- /EHsc)
  # CMake 把 CMAKE_CL_SHOWINCLUDES_PREFIX 按控制台 ACP 写入 ninja。
  # cl /utf-8 只改源文件编码，/showIncludes 仍跟控制台代码页走：CP936 输出 GBK，
  # UTF-8 控制台输出 UTF-8。Ninja 前缀写成 UTF-8，编译用 cl_utf8_launcher 切到 65001。
  if(CMAKE_GENERATOR MATCHES "Ninja")
    set(_tamias_showinc "娉ㄦ剰: 鍖呭惈鏂囦欢:  ")
    unset(CMAKE_CL_SHOWINCLUDES_PREFIX)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_tamias_showinc}" CACHE STRING
      "ACP-mojibake of UTF-8 注意: 包含文件:  (two spaces after colon)" FORCE)
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${_tamias_showinc}")
    set(CMAKE_C_CL_SHOWINCLUDES_PREFIX "${_tamias_showinc}")
    set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "${_tamias_showinc}")
    unset(_tamias_showinc)
    set(_tamias_cl_utf8_src "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cl_utf8_launcher.c")
    set(_tamias_cl_utf8 "${CMAKE_BINARY_DIR}/cl_utf8_launcher.exe")
    if("${_tamias_cl_utf8_src}" IS_NEWER_THAN "${_tamias_cl_utf8}")
      execute_process(
        COMMAND "${CMAKE_CXX_COMPILER}" /nologo /O1 /Fe${_tamias_cl_utf8} "${_tamias_cl_utf8_src}"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
        RESULT_VARIABLE _tamias_cl_utf8_rc
        OUTPUT_VARIABLE _tamias_cl_utf8_out
        ERROR_VARIABLE _tamias_cl_utf8_out
      )
      if(NOT _tamias_cl_utf8_rc EQUAL 0)
        message(WARNING "cl_utf8_launcher failed to build:\n${_tamias_cl_utf8_out}")
        unset(_tamias_cl_utf8)
      endif()
    endif()
  endif()
  # CMake 4 leaves CMAKE_<LANG>_FLAGS_<CONFIG> empty; vcpkg also blanks
  # CMAKE_MSVC_DEBUG_INFORMATION_FORMAT. RelWithDebInfo then has no /Zi or
  # /DEBUG, so Cursor/cppvsdbg breakpoints show "no debug info".
  add_compile_options("$<$<CONFIG:Debug,RelWithDebInfo>:/Zi>")
  add_compile_options("$<$<CONFIG:RelWithDebInfo>:/O2;/Ob1>")
  add_compile_definitions("$<$<CONFIG:Release,RelWithDebInfo,MinSizeRel>:NDEBUG>")
  add_link_options("$<$<CONFIG:Debug,RelWithDebInfo>:/DEBUG:FULL>")
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(TAMIAS_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(TAMIAS_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

option(TAMIAS_ENABLE_VULKAN_BACKEND "Build the Vulkan RHI backend." ON)
option(TAMIAS_ENABLE_OPENGL_BACKEND "Build the OpenGL RHI backend." ON)
# 浏览器两条后端：**WebGL 是默认**，WebGPU 是实验（见 docs/WEB.md、docs/WGPU.md）。
# 默认值做成 EMSCRIPTEN 感知，好让裸 emcmake（不带预设）也能编出一个能跑的后端。
if(EMSCRIPTEN)
  set(_tamias_webgl_default ON)
else()
  set(_tamias_webgl_default OFF)
endif()
option(TAMIAS_ENABLE_WEBGL_BACKEND "Build the WebGL2 RHI backend (Emscripten; the WASM default)." ${_tamias_webgl_default})
option(TAMIAS_ENABLE_WEBGPU_BACKEND "Build the browser WebGPU RHI backend (Emscripten; experimental)." OFF)
unset(_tamias_webgl_default)
# 建模内核：**OCCT 是默认**，Truck 是可选回退。
# wasm 上「有没有 OCCT」取决于有没有一份交叉编译出来的 OCCT，所以这里的默认值做成
# EMSCRIPTEN 感知——但**不能 FORCE**：选哪个内核是预设 / -D 的事。老版本正是在
# EMSCRIPTEN 分支里把 OCCT FORCE 成 OFF，结果不管预设怎么设，wasm 里永远只有 Truck。
if(EMSCRIPTEN)
  set(_tamias_occt_default OFF)
else()
  set(_tamias_occt_default ON)
endif()
option(TAMIAS_ENABLE_OCCT "Build the OCCT modeling kernel backend." ${_tamias_occt_default})
unset(_tamias_occt_default)
option(TAMIAS_ENABLE_TRUCK_KERNEL "Build the Truck (Rust) modeling kernel backend." OFF)
# OCCT 的导入路径（STEP / IGES / BREP + XCAF 颜色，见 occt_shape_ops.cpp）。
# 它比内核本身重得多：要 XCAF，而 XCAF 在 OCCT 7.9 上还会把 Visualization
# （TKService / TKV3d）一起拖进来。wasm 上根本没有这条路（服务端的 load_bytes 也只认
# .tdoc / .trscn / .obj），关掉能省一大截体积和依赖。
if(EMSCRIPTEN)
  set(_tamias_occt_de_default OFF)
else()
  set(_tamias_occt_de_default ON)
endif()
option(TAMIAS_OCCT_DATA_EXCHANGE "Build the OCCT STEP/IGES/BREP import path (pulls in XCAF + Visualization)." ${_tamias_occt_de_default})
unset(_tamias_occt_de_default)
option(TAMIAS_BUILD_TESTS "Build unit tests." ON)
# 服务端宿主：把 Session 暴露成扁平 C ABI 的共享库，给 .NET 服务壳 P/Invoke。
# 见 docs/DECISION-SERVER-LANGUAGE.md。缺省关，不影响桌面 / wasm 两条产线。
option(TAMIAS_BUILD_SERVER "Build the headless server host (tamias_server shared library)." OFF)
option(TAMIAS_BUILD_SERVER_SHELL "Also publish the .NET server shell next to tamias_server." OFF)
option(TAMIAS_USE_FETCHCONTENT "Fetch third-party libs with FetchContent when not found." ON)
# IfcOpenShell 的 IfcParse（IFC 语义树 + tamias_ifc_dump，见 src/bim）。
# 它和 OCCT 是**两件事**，以前挂在 TAMIAS_ENABLE_OCCT 上，于是「wasm 想要 OCCT」
# 会顺带把 Boost 1.87 + IfcOpenShell 的 FetchContent 拉进来（wasm 上直接 FATAL）。
# 配置期很贵，不需要就别开。wasm 一律关。
if(EMSCRIPTEN)
  set(_tamias_ifc_default OFF)
else()
  set(_tamias_ifc_default ON)
endif()
option(TAMIAS_ENABLE_IFC "Build IfcOpenShell IfcParse (IFC spatial tree + tamias_ifc_dump)." ${_tamias_ifc_default})
unset(_tamias_ifc_default)
option(TAMIAS_COMPILER_CACHE "Use sccache/ccache as compiler launcher when found." ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasPch.cmake")

if(TAMIAS_COMPILER_CACHE AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT EMSCRIPTEN)
  find_program(TAMIAS_COMPILER_CACHE_PROGRAM NAMES sccache ccache)
endif()
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja" AND DEFINED _tamias_cl_utf8)
  set(_tamias_launchers "${_tamias_cl_utf8}")
  if(TAMIAS_COMPILER_CACHE AND TAMIAS_COMPILER_CACHE_PROGRAM)
    list(APPEND _tamias_launchers "${TAMIAS_COMPILER_CACHE_PROGRAM}")
  endif()
  set(CMAKE_C_COMPILER_LAUNCHER "${_tamias_launchers}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${_tamias_launchers}")
  unset(_tamias_launchers)
elseif(TAMIAS_COMPILER_CACHE AND TAMIAS_COMPILER_CACHE_PROGRAM AND NOT CMAKE_CXX_COMPILER_LAUNCHER AND NOT EMSCRIPTEN)
  set(CMAKE_C_COMPILER_LAUNCHER "${TAMIAS_COMPILER_CACHE_PROGRAM}")
  set(CMAKE_CXX_COMPILER_LAUNCHER "${TAMIAS_COMPILER_CACHE_PROGRAM}")
endif()

if(EMSCRIPTEN)
  set(TAMIAS_ENABLE_VULKAN_BACKEND OFF CACHE BOOL "Vulkan is unavailable on WebAssembly." FORCE)
  set(TAMIAS_ENABLE_OPENGL_BACKEND OFF CACHE BOOL "Desktop OpenGL is unavailable on WebAssembly." FORCE)
  # WebGL / WebGPU 这两个**不能 FORCE**：wasm 与 wasm-webgpu 两个预设就是靠这两个开关
  # 选后端的，FORCE 会把预设的选择直接盖掉。老版本正是这么写的，结果 wasm 被钉死在
  # WebGPU 上，rhi/webgl 从来没在这个预设下编过（产物里 GLctx 零命中）。
  #
  # OCCT 同一个道理，**也不能在这里 FORCE 关**：wasm 预设要靠 TAMIAS_ENABLE_OCCT 选内核
  # ——有 wasm 的 OCCT 时它是默认、Truck 是可选回退，没有时只有 Truck。老版本在这里
  # FORCE 成 OFF，于是无论预设怎么设，wasm 里永远只有 Truck。一份 wasm 的 OCCT 怎么编、
  # 怎么指给构建（TAMIAS_OCCT_PREFIX），见 docs/WEB.md。
  set(TAMIAS_BUILD_TESTS OFF CACHE BOOL "Desktop tests are not built for WASM." FORCE)
  set(TAMIAS_USE_FETCHCONTENT OFF CACHE BOOL "No FetchContent on the WASM viewer path." FORCE)
  if(NOT TAMIAS_ENABLE_WEBGL_BACKEND AND NOT TAMIAS_ENABLE_WEBGPU_BACKEND)
    message(FATAL_ERROR
      "No WASM RHI backend selected. The `wasm` preset builds WebGL (the default); "
      "the `wasm-webgpu` preset builds the experimental WebGPU backend. Pass "
      "-DTAMIAS_ENABLE_WEBGL_BACKEND=ON (or ...WEBGPU_BACKEND=ON) to pick one.")
  endif()
endif()

# CMAKE_TOOLCHAIN_FILE in the cache does not mean the toolchain ran.
# An old build/ created without vcpkg will not load a newly added toolchain.
if(NOT EMSCRIPTEN AND NOT DEFINED Z_VCPKG_ROOT_DIR AND NOT DEFINED VCPKG_INSTALLED_DIR)
  message(FATAL_ERROR
    "vcpkg toolchain did not load. Delete the build directory and configure "
    "again with `cmake --preset msvc` or `cmake --preset linux` "
    "(Windows: use C:/dev/vcpkg, not the Visual Studio bundled vcpkg). "
    "Adding CMAKE_TOOLCHAIN_FILE to an existing cache is not enough.")
endif()
if(WIN32 AND DEFINED Z_VCPKG_ROOT_DIR AND Z_VCPKG_ROOT_DIR MATCHES "Microsoft Visual Studio")
  message(FATAL_ERROR
    "Do not use the Visual Studio bundled vcpkg. Point VCPKG_ROOT at C:/dev/vcpkg "
    "(got ${Z_VCPKG_ROOT_DIR}).")
endif()

include(CTest)
include(FetchContent)
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasShaders.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasDependencies.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasTacUI.cmake")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasTracy.cmake")

# 图形后端选择组件（决策 / 探测 / 降级）：引擎的 RHI 依赖它，所以要排在 engine 之前。
add_subdirectory(src/rhi_selection)
add_subdirectory(src/engine)
add_subdirectory(src/entity)
add_subdirectory(src/command)
add_subdirectory(src/mcp)
# AI 协议层（Meshy 等外部生成的请求 / 回包）：纯 C++，无 Qt，排在 mcp 之后（要用它的 Json）。
add_subdirectory(src/ai)
# 检索挂在 MCP 门面的读侧，所以排在 mcp 之后；无 Qt / 无 OCCT，wasm 分支也要它。
add_subdirectory(src/rag)
add_subdirectory(src/host)
# tac：自研界面库（契约层），无 Qt 无 OCCT，wasm 分支也可以要它（见 docs/TAC.md）。
add_subdirectory(src/ui/tac)
# IfcOpenShell 的 IfcParse（IFC 语义树 + tamias_ifc_dump）。
# 以前这里是 if(TAMIAS_ENABLE_OCCT)——把「几何内核」和「IFC 语义」绑成了一件事，
# 于是 wasm 想开 OCCT 就会顺带要求 Boost + IfcOpenShell。现在各归各的开关。
if(TAMIAS_ENABLE_IFC)
  add_subdirectory(src/bim)
endif()
if(NOT EMSCRIPTEN)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasDotnet.cmake")
  tamias_find_nethost()
  # qt：tac 契约的 Qt 实现，桌面今天在跑的那套。
  add_subdirectory(src/ui/qt)
  add_subdirectory(src/plugin)
  # 服务端宿主：Session 的第四个宿主（Qt / embind / 应用内 MCP 之外的第四个）。
  if(TAMIAS_BUILD_SERVER)
    add_subdirectory(src/server)
  endif()
  add_subdirectory(src/app)
endif()
if(EMSCRIPTEN)
  add_subdirectory(src/wasm)
endif()

if(TAMIAS_BUILD_TESTS AND BUILD_TESTING)
  add_subdirectory(tests)
endif()

if(NOT EMSCRIPTEN)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasPackage.cmake")
endif()

tamias_apply_build_acceleration()

message(STATUS "Tamias Vulkan backend: ${TAMIAS_ENABLE_VULKAN_BACKEND}")
message(STATUS "Tamias OpenGL backend: ${TAMIAS_ENABLE_OPENGL_BACKEND}")
message(STATUS "Tamias WebGL backend: ${TAMIAS_ENABLE_WEBGL_BACKEND}")
message(STATUS "Tamias WebGPU backend: ${TAMIAS_ENABLE_WEBGPU_BACKEND}")
message(STATUS "Tamias OCCT: ${TAMIAS_OCCT_ROOT}")
message(STATUS "Tamias IfcOpenShell: IfcParse schemas=${TAMIAS_IFC_SCHEMAS}")
message(STATUS "Tamias PCH: ${TAMIAS_PCH_ACTIVE} (option ${TAMIAS_ENABLE_PCH})")
message(STATUS "Tamias unity build: ${TAMIAS_UNITY_BUILD}")
if(CMAKE_CXX_COMPILER_LAUNCHER)
  message(STATUS "Tamias compiler cache: ${CMAKE_CXX_COMPILER_LAUNCHER}")
else()
  message(STATUS "Tamias compiler cache: OFF")
endif()
