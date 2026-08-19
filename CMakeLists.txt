cmake_minimum_required(VERSION 3.24)
project(tamias VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 AND NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR "Tamias currently supports Windows and Linux only.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_options(/utf-8 /MP /FS /W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(TAMIAS_SHADER_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(TAMIAS_SHADER_OUTPUT_DIR "${CMAKE_BINARY_DIR}/shaders")

option(TAMIAS_ENABLE_VULKAN_BACKEND "Build the Vulkan RHI backend." ON)
option(TAMIAS_ENABLE_OPENGL_BACKEND "Build the OpenGL RHI backend." ON)
option(TAMIAS_BUILD_TESTS "Build unit tests." ON)
option(TAMIAS_USE_FETCHCONTENT "Fetch third-party libs with FetchContent when not found." ON)

# CMAKE_TOOLCHAIN_FILE in the cache does not mean the toolchain ran.
# An old build/ created without vcpkg will not load a newly added toolchain.
if(NOT DEFINED Z_VCPKG_ROOT_DIR AND NOT DEFINED VCPKG_INSTALLED_DIR)
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
add_subdirectory(src/bim)
add_subdirectory(src/command)
add_subdirectory(src/app)

if(TAMIAS_BUILD_TESTS AND BUILD_TESTING)
  add_subdirectory(tests)
endif()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/TamiasPackage.cmake")

message(STATUS "Tamias Vulkan backend: ${TAMIAS_ENABLE_VULKAN_BACKEND}")
message(STATUS "Tamias OpenGL backend: ${TAMIAS_ENABLE_OPENGL_BACKEND}")
message(STATUS "Tamias OCCT: ${TAMIAS_OCCT_ROOT}")
message(STATUS "Tamias IfcOpenShell: IfcParse schemas=${TAMIAS_IFC_SCHEMAS}")
