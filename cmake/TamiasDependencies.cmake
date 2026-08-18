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
# Leftover official-layout cache from OCCT_ROOT must not win over vcpkg.
unset(OpenCASCADE_DIR CACHE)
unset(opencascade_DIR CACHE)

find_package(Vulkan REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg LinguistTools)

set(TAMIAS_THIRDPARTY_DIR "${CMAKE_SOURCE_DIR}/3rdparty")

# VMA header vendored under 3rdparty/
if(NOT EXISTS "${TAMIAS_THIRDPARTY_DIR}/vk_mem_alloc.h")
  message(FATAL_ERROR "Missing 3rdparty/vk_mem_alloc.h")
endif()
add_library(VulkanMemoryAllocator INTERFACE)
add_library(GPUOpen::VulkanMemoryAllocator ALIAS VulkanMemoryAllocator)
target_include_directories(VulkanMemoryAllocator INTERFACE "${TAMIAS_THIRDPARTY_DIR}")

# rapidobj single header as rapidobj/rapidobj.hpp
# An IntelliSense-only configuration may install a clangd stub instead.
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

find_program(TAMIAS_DXC NAMES dxc
  HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")

# --- OCCT (required, via vcpkg opencascade) ---
find_package(OpenCASCADE CONFIG QUIET)
if(NOT OpenCASCADE_FOUND)
  find_package(opencascade CONFIG QUIET)
endif()
if(NOT OpenCASCADE_FOUND AND NOT opencascade_FOUND)
  message(FATAL_ERROR
    "OpenCASCADE not found in the vcpkg install prefix.\n"
    "  VCPKG_INSTALLED_DIR=${VCPKG_INSTALLED_DIR}\n"
    "  VCPKG_TARGET_TRIPLET=${VCPKG_TARGET_TRIPLET}\n"
    "  CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}\n"
    "If this build directory predates the vcpkg OCCT switch, delete it and "
    "reconfigure so vcpkg can install opencascade from vcpkg.json.")
endif()

# STEP/IGES toolkits pull XCAF/Visualization transitively on OCCT 7.9.
set(TAMIAS_OCCT_LIBRARIES
  TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
  TKGeomAlgo TKTopAlgo TKPrim TKFillet TKBO TKBool TKMesh TKShHealing TKHLR
  TKService TKV3d
  TKCDF TKLCAF TKCAF TKVCAF TKXCAF
  TKDE TKXSBase TKDESTEP TKDEIGES)
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

message(STATUS "OCCT found: ${TAMIAS_OCCT_ROOT}")
