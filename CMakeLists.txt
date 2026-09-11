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
option(TAMIAS_ENABLE_WEBGL_BACKEND "Build the WebGL2 RHI backend (Emscripten)." OFF)
option(TAMIAS_ENABLE_WEBGPU_BACKEND "Build the browser WebGPU RHI backend (Emscripten)." OFF)
option(TAMIAS_ENABLE_OCCT "Build OCCT modeling and IfcOpenShell." ON)
option(TAMIAS_BUILD_TESTS "Build unit tests." ON)
option(TAMIAS_USE_FETCHCONTENT "Fetch third-party libs with FetchContent when not found." ON)
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
  set(TAMIAS_ENABLE_WEBGL_BACKEND OFF CACHE BOOL "WebGL2 is the optional WASM fallback." FORCE)
  set(TAMIAS_ENABLE_WEBGPU_BACKEND ON CACHE BOOL "WebGPU is the WASM RHI." FORCE)
  set(TAMIAS_ENABLE_OCCT OFF CACHE BOOL "OCCT is not linked in the phase-1 viewer." FORCE)
  set(TAMIAS_BUILD_TESTS OFF CACHE BOOL "Desktop tests are not built for WASM." FORCE)
  set(TAMIAS_USE_FETCHCONTENT OFF CACHE BOOL "No FetchContent on the WASM viewer path." FORCE)
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

add_subdirectory(src/engine)
add_subdirectory(src/entity)
add_subdirectory(src/command)
add_subdirectory(src/host)
if(TAMIAS_ENABLE_OCCT)
  add_subdirectory(src/bim)
endif()
if(NOT EMSCRIPTEN)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasDotnet.cmake")
  tamias_find_nethost()
  add_subdirectory(src/plugin)
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
