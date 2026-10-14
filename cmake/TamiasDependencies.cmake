# Resolve Qt, Vulkan, and local/third-party headers.

# System Qt on Windows. Do not put Qt on CMAKE_PREFIX_PATH in presets —
# that cache overwrite hides vcpkg's OpenCASCADE.
set(TAMIAS_QT_PREFIX "" CACHE PATH "Optional Qt prefix (Windows system Qt).")
if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()
if(TAMIAS_QT_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${TAMIAS_QT_PREFIX}")
endif()
# OCCT 的安装前缀。桌面 / 服务端不用给（走 vcpkg 的 prefix）；**wasm 必须给**，
# 指一份交叉编译到 wasm32-emscripten 的 OCCT（怎么编见 docs/WEB.md）。
# 走 CMAKE_PREFIX_PATH 而不是 OpenCASCADE_DIR：下面那两行 unset 会把
# OpenCASCADE_DIR 的缓存清掉（防止旧的 official-layout 缓存压过 vcpkg），
# 从命令行塞 -DOpenCASCADE_DIR=... 会被一起清掉，等于白设。
set(TAMIAS_OCCT_PREFIX "" CACHE PATH "Optional OCCT install prefix (point at a cross-compiled wasm OCCT).")
if(TAMIAS_OCCT_PREFIX)
  list(PREPEND CMAKE_PREFIX_PATH "${TAMIAS_OCCT_PREFIX}")
  # Emscripten 的工具链把 CMAKE_FIND_ROOT_PATH_MODE_PACKAGE 设成 ONLY
  # （Emscripten.cmake 末尾那三行），于是 find_package 只在 sysroot 里翻，
  # CMAKE_PREFIX_PATH 被整个无视——症状是 "Could NOT find OpenCASCADE (missing:
  # OpenCASCADE_DIR)"，**连候选文件都不列**，很容易误判成"OCCT 没装对"。
  # 所以 prefix 还得进 CMAKE_FIND_ROOT_PATH，并把 package 搜索放开成 BOTH
  # （sysroot 与 prefix 都找）。实测：不加这三行永远找不到。
  list(APPEND CMAKE_FIND_ROOT_PATH "${TAMIAS_OCCT_PREFIX}")
  set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
endif()
# Leftover official-layout cache from OCCT_ROOT must not win over vcpkg.
unset(OpenCASCADE_DIR CACHE)
unset(opencascade_DIR CACHE)

if(NOT EMSCRIPTEN)
  if(TAMIAS_ENABLE_VULKAN_BACKEND)
    find_package(Vulkan REQUIRED)
  endif()
  # Network: 内置 MCP 服务的 loopback HTTP 传输（见 docs/DECISION-AI-INTEGRATION.md）。
  find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg LinguistTools Network)
endif()

set(TAMIAS_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

# VMA header vendored under 3rdparty/
if(NOT EXISTS "${TAMIAS_THIRDPARTY_DIR}/vk_mem_alloc.h")
  message(FATAL_ERROR "Missing 3rdparty/vk_mem_alloc.h")
endif()
add_library(VulkanMemoryAllocator INTERFACE)
add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)
target_include_directories(VulkanMemoryAllocator INTERFACE "${TAMIAS_THIRDPARTY_DIR}")

# rapidobj single header as rapidobj/rapidobj.hpp
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/thirdparty_include/rapidobj")
configure_file(
  "${TAMIAS_THIRDPARTY_DIR}/rapidobj.hpp"
  "${CMAKE_BINARY_DIR}/thirdparty_include/rapidobj/rapidobj.hpp"
  COPYONLY)
set(RAPIDOBJ_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/thirdparty_include" CACHE PATH "" FORCE)

if(TAMIAS_BUILD_TESTS AND NOT EMSCRIPTEN)
  find_package(GTest CONFIG QUIET)
  if(NOT TARGET GTest::gtest)
    if(TAMIAS_USE_FETCHCONTENT)
      include(FetchContent)
      set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
      set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
      FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
      FetchContent_MakeAvailable(googletest)
    else()
      message(FATAL_ERROR "GTest not found (enable TAMIAS_USE_FETCHCONTENT or install gtest)")
    endif()
  endif()
endif()

find_program(TAMIAS_DXC NAMES dxc
  HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

if(NOT TAMIAS_ENABLE_OCCT)
  set(TAMIAS_OCCT_ROOT "(disabled)")
  set(TAMIAS_OCCT_INCLUDE_DIR "")
  set(TAMIAS_OCCT_LIBRARIES "")
  set(TAMIAS_OCCT_RUNTIME_PATH "")
  set(TAMIAS_IFC_SCHEMAS "(disabled)")
  return()
endif()

# --- OCCT (required, via vcpkg opencascade) ---
find_package(OpenCASCADE CONFIG QUIET)
if(NOT OpenCASCADE_FOUND)
  find_package(opencascade CONFIG QUIET)
endif()
if(NOT OpenCASCADE_FOUND AND NOT opencascade_FOUND)
  if(EMSCRIPTEN)
    message(FATAL_ERROR
      "wasm 上开了 TAMIAS_ENABLE_OCCT，但没有找到 OCCT。\n"
      "浏览器线要的是一份**交叉编译到 wasm32-emscripten** 的 OCCT"
      "（FoundationClasses + ModelingData + ModelingAlgorithms 就够，\n"
      "STEP/IGES 那组不需要，见 TAMIAS_OCCT_DATA_EXCHANGE），"
      "再用 -DTAMIAS_OCCT_PREFIX=<prefix> 指过来。\n"
      "怎么编见 docs/WEB.md。只要 Truck 就把 TAMIAS_ENABLE_OCCT 关掉。")
  endif()
  message(FATAL_ERROR
    "OpenCASCADE not found in the vcpkg install prefix.\n"
    "  VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}\n"
    "  VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}\n"
    "  CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}\n"
    "If this build directory predates the vcpkg OCCT switch, delete it and "
    "reconfigure so vcpkg can install opencascade from vcpkg.json.")
endif()

# 内核本身要的（occt_kernel.cpp / occt_mesh.cpp）。这一组就是**最小可造型集**：
# 一份只编了 FoundationClasses + ModelingData + ModelingAlgorithms 的 OCCT 就够
# ——服务端 tamias_server.dll 实测导入的正好是这一组加它们的传递依赖。
set(_tamias_occt_toolkits_core
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
  TKGeomAlgo TKTopAlgo TKPrim TKFillet TKBO TKBool TKMesh TKShHealing TKHLR)

# STEP / IGES / BREP 导入额外要的（occt_shape_ops.cpp 用的 XCAF）。
# OCCT 7.9 上 XCAF 会把 Visualization 一并拖进来（TKService / TKV3d），
# 所以这一组是绑在一起的，拆不开。
set(_tamias_occt_toolkits_data_exchange
  TKService TKV3d
  TKCDF TKLCAF TKCAF TKVCAF TKXCAF
  TKDE TKXSBase TKDESTEP TKDEIGES)

set(TAMIAS_OCCT_LIBRARIES ${_tamias_occt_toolkits_core})
if(TAMIAS_OCCT_DATA_EXCHANGE)
  list(APPEND TAMIAS_OCCT_LIBRARIES ${_tamias_occt_toolkits_data_exchange})
endif()
foreach(_lib IN LISTS TAMIAS_OCCT_LIBRARIES)
  if(NOT TARGET ${_lib})
    message(FATAL_ERROR "OpenCASCADE toolkit missing: ${_lib}")
  endif()
endforeach()

if(OpenCASCADE_INCLUDE_DIR)
  set(TAMIAS_OCCT_INCLUDE_DIR "${OpenCASCADE_INCLUDE_DIR}")
elseif(opencascade_INCLUDE_DIR)
  set(TAMIAS_OCCT_INCLUDE_DIR "${opencascade_INCLUDE_DIR}")
else()
  set(TAMIAS_OCCT_INCLUDE_DIR "")
endif()
if(NOT TAMIAS_OCCT_INCLUDE_DIR AND TARGET TKernel)
  get_target_property(_tamias_occt_incs TKernel INTERFACE_INCLUDE_DIRECTORIES)
  if(_tamias_occt_incs)
    list(GET _tamias_occt_incs 0 TAMIAS_OCCT_INCLUDE_DIR)
  endif()
endif()

if(DEFINED OpenCASCADE_INSTALL_PREFIX AND NOT OpenCASCADE_INSTALL_PREFIX STREQUAL "")
  set(TAMIAS_OCCT_ROOT "${OpenCASCADE_INSTALL_PREFIX}")
elseif(DEFINED OpenCASCADE_DIR)
  get_filename_component(TAMIAS_OCCT_ROOT "${OpenCASCADE_DIR}/../../.." ABSOLUTE)
else()
  set(TAMIAS_OCCT_ROOT "(vcpkg)")
endif()

# ctest PRE_TEST discovery needs OCCT/freetype on PATH even before POST_BUILD copy.
set(TAMIAS_OCCT_RUNTIME_PATH "")
if(WIN32 AND DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
  list(APPEND TAMIAS_OCCT_RUNTIME_PATH
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin")
elseif(DEFINED OpenCASCADE_BINARY_DIR)
  list(APPEND TAMIAS_OCCT_RUNTIME_PATH "${OpenCASCADE_BINARY_DIR}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/TamiasCopyOcctRuntime.cmake")
# IfcOpenShell 是独立的开关（见根 CMakeLists 的 TAMIAS_ENABLE_IFC）：它拉 Boost +
# IfcOpenShell 的 FetchContent，和「有没有几何内核」不是一回事。
if(TAMIAS_ENABLE_IFC)
  include("${CMAKE_CURRENT_LIST_DIR}/TamiasIfcOpenShell.cmake")
else()
  set(TAMIAS_IFC_SCHEMAS "(disabled)")
endif()

message(STATUS "OCCT found: ${TAMIAS_OCCT_ROOT}")
