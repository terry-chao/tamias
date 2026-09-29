# C++20 模块试点（tamias::math）的独立脚手架。
#
# 只引用 src/engine/math/ 和 tests/math_module/，不拉 vcpkg / Qt / OCCT，configure 秒级
# 完成，用来回答“这台机器 / 这个工具链到底能不能编 C++20 模块”，也是试点 CI 的入口。
#
#   cmake -S tests/math_module/standalone -B build/math-module -G Ninja -DCMAKE_BUILD_TYPE=Debug
#   cmake --build build/math-module
#   ctest --test-dir build/math-module --output-on-failure
#
# 离线/复用时可以指到已有 gtest 源码树，省掉 FetchContent：
#   -DFETCHCONTENT_SOURCE_DIR_GOOGLETEST=<path/to/googletest>
cmake_minimum_required(VERSION 3.28)
project(tamias_math_module_pilot LANGUAGES CXX)

get_filename_component(TAMIAS_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE
  CACHE PATH "tamias checkout root")

if(NOT EXISTS "${TAMIAS_ROOT}/src/engine/math/tamias.math.cppm")
  message(FATAL_ERROR "tamias checkout not found at ${TAMIAS_ROOT}")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 与主项目根 CMakeLists 保持一致：MSVC 少了 /utf-8，中文注释会被按本地代码页解析。
if(MSVC)
  add_compile_options(/utf-8 /W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

add_library(tamias_math_module)
target_sources(tamias_math_module
  PUBLIC
    FILE_SET CXX_MODULES BASE_DIRS ${TAMIAS_ROOT}/src
    FILES ${TAMIAS_ROOT}/src/engine/math/tamias.math.cppm
)
target_include_directories(tamias_math_module PUBLIC ${TAMIAS_ROOT}/src)

find_package(GTest CONFIG QUIET)
if(NOT TARGET GTest::gtest_main)
  include(FetchContent)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip)
  FetchContent_MakeAvailable(googletest)
endif()

add_executable(math_module_tests
  ${TAMIAS_ROOT}/tests/math_module_parity_tests.cpp
  ${TAMIAS_ROOT}/tests/math_module/header_side.cpp
  ${TAMIAS_ROOT}/tests/math_module/module_side.cpp
  ${TAMIAS_ROOT}/tests/math_module/parity_check.cpp)
target_include_directories(math_module_tests PRIVATE ${TAMIAS_ROOT}/tests)
target_link_libraries(math_module_tests PRIVATE tamias_math_module GTest::gtest_main)

enable_testing()
add_test(NAME math_module_tests COMMAND math_module_tests)
