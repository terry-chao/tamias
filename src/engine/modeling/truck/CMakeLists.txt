# Truck 后端（Rust）：C++ 转接层 + cargo 编出来的静态库。
# 依赖 Rust 工具链；没装 cargo 或没网时不要打开 TAMIAS_ENABLE_TRUCK_KERNEL。
find_program(TAMIAS_CARGO_EXECUTABLE cargo)
if(NOT TAMIAS_CARGO_EXECUTABLE)
  message(WARNING "TAMIAS_ENABLE_TRUCK_KERNEL=ON 但找不到 cargo，Truck 后端被跳过")
  return()
endif()

set(_truck_bridge_dir "${CMAKE_CURRENT_SOURCE_DIR}/../truck-bridge")
if(WIN32)
  set(_truck_bridge_lib "${_truck_bridge_dir}/target/release/tamias_truck_bridge.lib")
else()
  set(_truck_bridge_lib "${_truck_bridge_dir}/target/release/libtamias_truck_bridge.a")
endif()

# 声明 Rust 侧依赖：不写 DEPENDS，改了 lib.rs 之后 cargo 不会重跑，
# 链接的还是旧静态库（踩过一次）。
file(GLOB_RECURSE _truck_bridge_sources CONFIGURE_DEPENDS
  "${_truck_bridge_dir}/src/*.rs"
  "${_truck_bridge_dir}/Cargo.toml"
  "${_truck_bridge_dir}/Cargo.lock"
)
add_custom_command(
  OUTPUT "${_truck_bridge_lib}"
  COMMAND "${TAMIAS_CARGO_EXECUTABLE}" build --release
  WORKING_DIRECTORY "${_truck_bridge_dir}"
  DEPENDS ${_truck_bridge_sources}
  COMMENT "Building Truck bridge (cargo, Rust)"
  VERBATIM
)
add_custom_target(tamias_truck_bridge_build DEPENDS "${_truck_bridge_lib}")

add_library(tamias_kernel_truck STATIC truck_kernel.cpp)
add_library(tamias::kernel_truck ALIAS tamias_kernel_truck)
add_dependencies(tamias_kernel_truck tamias_truck_bridge_build)
target_include_directories(tamias_kernel_truck PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(tamias_kernel_truck
  PUBLIC
    tamias::kernel
    tamias::core
    tamias::graphics
    "${_truck_bridge_lib}"
)
# Rust std 在 MSVC 上要这些系统库；缺哪个链接器会报出来（synchronize 在 Windows SDK
# 里没有对应 .lib，别加）。
if(WIN32)
  target_link_libraries(tamias_kernel_truck PUBLIC
    ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 kernel32)
endif()
