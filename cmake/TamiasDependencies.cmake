# Resolve Qt, Vulkan, and local/third-party headers.

find_package(Vulkan REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg)

set(TAMIAS_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

# VMA header vendored under 3rdparty/
if(NOT EXISTS "${TAMIAS_THIRDPARTY_DIR}/vk_mem_alloc.h")
  message(FATAL_ERROR "Missing 3rdparty/vk_mem_alloc.h")
endif()
add_library(VulkanMemoryAllocator INTERFACE)
add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)
target_include_directories(VulkanMemoryAllocator INTERFACE "${TAMIAS_THIRDPARTY_DIR}")

# rapidobj single header as rapidobj/rapidobj.hpp
# msvc-ninja may install a clangd stub instead — real header crashes some clangd builds.
option(TAMIAS_CLANGD_RAPIDOBJ_STUB
  "Install clangd-safe rapidobj stub into the build tree (IntelliSense only)." OFF)
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/thirdparty_include/rapidobj")
if(TAMIAS_CLANGD_RAPIDOBJ_STUB)
  set(_tamias_rapidobj_src "${CMAKE_SOURCE_DIR}/tools/clangd-stubs/rapidobj/rapidobj.hpp")
  message(STATUS "rapidobj: using clangd stub (not for production builds)")
else()
  set(_tamias_rapidobj_src "${TAMIAS_THIRDPARTY_DIR}/rapidobj.hpp")
endif()
configure_file(
  "${_tamias_rapidobj_src}"
  "${CMAKE_BINARY_DIR}/thirdparty_include/rapidobj/rapidobj.hpp"
  COPYONLY)
set(RAPIDOBJ_INCLUDE_DIRS "${CMAKE_BINARY_DIR}/thirdparty_include" CACHE PATH "" FORCE)

if(TAMIAS_BUILD_TESTS)
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

find_program(TAMIAS_GLSLANG_VALIDATOR NAMES glslangValidator
  HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
