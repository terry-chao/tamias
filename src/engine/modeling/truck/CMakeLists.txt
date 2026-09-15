# Truck 后端（Rust）：C++ 转接层 + cargo 编出来的静态库。
# 依赖 Rust 工具链；没装 cargo 或没网时不要打开 TAMIAS_ENABLE_TRUCK_KERNEL。
find_program(TAMIAS_CARGO_EXECUTABLE cargo)
if(NOT TAMIAS_CARGO_EXECUTABLE)
  message(WARNING "TAMIAS_ENABLE_TRUCK_KERNEL=ON 但找不到 cargo，Truck 后端被跳过")
  return()
endif()

set(_truck_bridge_dir "${CMAKE_CURRENT_SOURCE_DIR}/../truck-bridge")

# 桌面：宿主目标 + release（panic=unwind，桥靠 catch_unwind 把 panic 转成错误码）。
# WASM：wasm32-unknown-emscripten + wasm-release（panic=abort）。Rust 的 wasm std
# 用旧的 emscripten JS 异常运行时（__cpp_exception），Emscripten 6.x 不再提供，
# 所以 wasm 侧不能在 unwind 下链接；见 truck-bridge/Cargo.toml 的注释。
if(EMSCRIPTEN)
  set(_truck_cargo_platform_args --target wasm32-unknown-emscripten)
  set(_truck_cargo_profile wasm-release)
  set(_truck_bridge_lib
      "${_truck_bridge_dir}/target/wasm32-unknown-emscripten/wasm-release/libtamias_truck_bridge.a")
  set(_truck_bridge_desc "wasm32-unknown-emscripten")
elseif(WIN32)
  set(_truck_cargo_profile release)
  set(_truck_bridge_lib "${_truck_bridge_dir}/target/release/tamias_truck_bridge.lib")
  set(_truck_bridge_desc "host")
else()
  set(_truck_cargo_profile release)
  set(_truck_bridge_lib "${_truck_bridge_dir}/target/release/libtamias_truck_bridge.a")
  set(_truck_bridge_desc "host")
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
  COMMAND "${TAMIAS_CARGO_EXECUTABLE}" build ${_truck_cargo_platform_args}
          --profile ${_truck_cargo_profile}
  WORKING_DIRECTORY "${_truck_bridge_dir}"
  DEPENDS ${_truck_bridge_sources}
  COMMENT "Building Truck bridge (cargo, Rust, ${_truck_bridge_desc})"
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
    tamias::base
    tamias::graphics
    "${_truck_bridge_lib}"
)
# Rust std 在 MSVC 上要这些系统库；缺哪个链接器会报出来（synchronize 在 Windows SDK
# 里没有对应 .lib，别加）。
if(WIN32)
  target_link_libraries(tamias_kernel_truck PUBLIC
    ws2_32 userenv bcrypt ntdll advapi32 ole32 shell32 kernel32)
endif()
