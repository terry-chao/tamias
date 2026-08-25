cmake_minimum_required(VERSION 3.24)
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
option(TAMIAS_ENABLE_OCCT "Build OCCT modeling and IfcOpenShell." ON)
option(TAMIAS_BUILD_TESTS "Build unit tests." ON)
option(TAMIAS_USE_FETCHCONTENT "Fetch third-party libs with FetchContent when not found." ON)

if(EMSCRIPTEN)
  set(TAMIAS_ENABLE_VULKAN_BACKEND OFF CACHE BOOL "Vulkan is unavailable on WebAssembly." FORCE)
  set(TAMIAS_ENABLE_OPENGL_BACKEND OFF CACHE BOOL "Desktop OpenGL is unavailable on WebAssembly." FORCE)
  set(TAMIAS_ENABLE_WEBGL_BACKEND ON CACHE BOOL "WebGL2 is the WASM RHI." FORCE)
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
if(TAMIAS_ENABLE_OCCT)
  add_subdirectory(src/bim)
endif()
if(NOT EMSCRIPTEN)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasDotnet.cmake")
  tamias_find_nethost()
  add_subdirectory(src/command)
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

message(STATUS "Tamias Vulkan backend: ${TAMIAS_ENABLE_VULKAN_BACKEND}")
message(STATUS "Tamias OpenGL backend: ${TAMIAS_ENABLE_OPENGL_BACKEND}")
message(STATUS "Tamias WebGL backend: ${TAMIAS_ENABLE_WEBGL_BACKEND}")
message(STATUS "Tamias OCCT: ${TAMIAS_OCCT_ROOT}")
message(STATUS "Tamias IfcOpenShell: IfcParse schemas=${TAMIAS_IFC_SCHEMAS}")
