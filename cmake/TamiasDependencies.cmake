# Resolve Qt, Vulkan, and local/third-party headers.

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

# --- OCCT (optional, via OCCT_ROOT) ---
set(TAMIAS_HAS_OCCT OFF)
set(TAMIAS_OCCT_INCLUDE_DIR "")
set(TAMIAS_OCCT_LIBRARY_DIR "")
set(TAMIAS_OCCT_BIN_DIR "")
set(TAMIAS_OCCT_LIBRARIES "")

if(TAMIAS_ENABLE_OCCT)
  if(DEFINED ENV{OCCT_ROOT} AND NOT "$ENV{OCCT_ROOT}" STREQUAL "")
    set(_tamias_occt_root "$ENV{OCCT_ROOT}")
    file(TO_CMAKE_PATH "${_tamias_occt_root}" _tamias_occt_root)
    set(OpenCASCADE_DIR "${_tamias_occt_root}/cmake" CACHE PATH "OpenCASCADE cmake dir" FORCE)
    find_package(OpenCASCADE CONFIG REQUIRED)
    set(TAMIAS_OCCT_INCLUDE_DIR "${OpenCASCADE_INCLUDE_DIR}")
    if(WIN32)
      set(TAMIAS_OCCT_LIBRARY_DIR "${_tamias_occt_root}/win64/vc14/lib")
      set(TAMIAS_OCCT_LIBRARY_DIR_DEBUG "${_tamias_occt_root}/win64/vc14/libd")
      set(TAMIAS_OCCT_BIN_DIR "${_tamias_occt_root}/win64/vc14/bin")
      set(TAMIAS_OCCT_BIN_DIR_DEBUG "${_tamias_occt_root}/win64/vc14/bind")
    else()
      set(TAMIAS_OCCT_LIBRARY_DIR "${OpenCASCADE_LIBRARY_DIR}")
      set(TAMIAS_OCCT_BIN_DIR "${OpenCASCADE_BINARY_DIR}")
    endif()

    # STEP/IGES toolkits pull XCAF/Visualization transitively on OCCT 7.9.
    set(_tamias_occt_libs
      TKernel TKMath TKG2d TKG3d TKGeomBase TKBRep
      TKGeomAlgo TKTopAlgo TKPrim TKFillet TKBO TKBool TKMesh TKShHealing TKHLR
      TKService TKV3d
      TKCDF TKLCAF TKCAF TKVCAF TKXCAF
      TKDE TKXSBase TKDESTEP TKDEIGES)
    set(TAMIAS_OCCT_LIBRARIES "")
    foreach(_lib IN LISTS _tamias_occt_libs)
      if(WIN32 AND EXISTS "${TAMIAS_OCCT_LIBRARY_DIR_DEBUG}/${_lib}.lib")
        list(APPEND TAMIAS_OCCT_LIBRARIES
          optimized "${TAMIAS_OCCT_LIBRARY_DIR}/${_lib}.lib"
          debug "${TAMIAS_OCCT_LIBRARY_DIR_DEBUG}/${_lib}.lib")
      else()
        list(APPEND TAMIAS_OCCT_LIBRARIES "${_lib}")
      endif()
    endforeach()
    if(NOT WIN32)
      link_directories("${TAMIAS_OCCT_LIBRARY_DIR}")
    endif()

    # Sibling 3rdparty tree used by official OCCT Windows packages.
    set(_tamias_occt_3rdparty "${_tamias_occt_root}/../3rdparty-vc14-64")
    file(TO_CMAKE_PATH "${_tamias_occt_3rdparty}" _tamias_occt_3rdparty)
    if(EXISTS "${_tamias_occt_3rdparty}")
      set(TAMIAS_OCCT_3RDPARTY_ROOT "${_tamias_occt_3rdparty}")
    else()
      set(TAMIAS_OCCT_3RDPARTY_ROOT "")
    endif()
    set(TAMIAS_OCCT_RUNTIME_PATH "")
    if(WIN32)
      list(APPEND TAMIAS_OCCT_RUNTIME_PATH
        "${TAMIAS_OCCT_BIN_DIR_DEBUG}" "${TAMIAS_OCCT_BIN_DIR}")
    else()
      list(APPEND TAMIAS_OCCT_RUNTIME_PATH "${TAMIAS_OCCT_BIN_DIR}")
    endif()
    if(EXISTS "${_tamias_occt_3rdparty}")
      file(GLOB _tamias_occt_3rd_bins
        "${_tamias_occt_3rdparty}/*/bin"
        "${_tamias_occt_3rdparty}/*/bind"
        "${_tamias_occt_3rdparty}/*/bin/win64"
        "${_tamias_occt_3rdparty}/*/debug/bin")
      list(APPEND TAMIAS_OCCT_RUNTIME_PATH ${_tamias_occt_3rd_bins})
      message(STATUS "OCCT 3rdparty: ${_tamias_occt_3rdparty}")
    endif()
    # Semicolon-separated PATH prefix for VS debugger / ctest / launch.
    set(TAMIAS_OCCT_RUNTIME_PATH_STRING "")
    foreach(_p IN LISTS TAMIAS_OCCT_RUNTIME_PATH)
      if(TAMIAS_OCCT_RUNTIME_PATH_STRING STREQUAL "")
        set(TAMIAS_OCCT_RUNTIME_PATH_STRING "${_p}")
      else()
        set(TAMIAS_OCCT_RUNTIME_PATH_STRING "${TAMIAS_OCCT_RUNTIME_PATH_STRING};${_p}")
      endif()
    endforeach()

    set(TAMIAS_HAS_OCCT ON)
    message(STATUS "OCCT found: ${_tamias_occt_root}")
  else()
    message(STATUS "OCCT disabled: set environment variable OCCT_ROOT to enable")
  endif()
endif()
