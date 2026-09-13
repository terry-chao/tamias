#pragma once

// Truck 桥的 C ABI（实现在 ../../truck-bridge/src/lib.rs，Rust）。
// 这半边只做转发：不持有业务逻辑，错误码 + truck_last_error() 回传。
// 布局与 Rust 侧的 #[repr(C)] 一一对应，改一边必须改另一边
// （重新生成可以用：cargo install cbindgen && cbindgen --config cbindgen.toml --output truck_bridge.h）。

#include <cstddef>
#include <cstdint>

extern "C" {

// 与 Rust 的 TruckEdgeMeasure 同布局：key 是「同一条几何边」的标识。
struct TruckEdgeMeasure {
  std::uint64_t key;
  double mid[3];
  double dir[3];
  std::int32_t has_dir;
  double length;
  double n1[3];
  std::int32_t has_n1;
  double n2[3];
  std::int32_t has_n2;
};

enum {
  TRUCK_OK = 0,
  TRUCK_E_FAILED = 1,
  TRUCK_E_PANIC = 2,
  TRUCK_E_BAD_ARG = 3,
  TRUCK_E_UNSUPPORTED = 4,
  TRUCK_E_BAD_HANDLE = 5,
};

const char* truck_version(void);
const char* truck_last_error(void);

std::int32_t truck_rect_face(double width, double height, std::uint64_t* out);
std::int32_t truck_circle_face(double radius, std::uint64_t* out);
std::int32_t truck_polygon_face(const double* xyz, std::size_t count, std::uint64_t* out);
std::int32_t truck_extrude(std::uint64_t body, double depth, std::uint64_t* out);
std::int32_t truck_transform(std::uint64_t body, double x, double y, double z,
                             std::uint64_t* out);
std::int32_t truck_body_release(std::uint64_t body);

std::int32_t truck_bounds(std::uint64_t body, double* min_xyz, double* max_xyz);
std::int32_t truck_edge_count(std::uint64_t body, std::size_t* count);
std::int32_t truck_measure_edges(std::uint64_t body, TruckEdgeMeasure* out,
                                 std::size_t capacity, std::size_t* written);
std::int32_t truck_tessellate(std::uint64_t body, double deflection, float** verts,
                              std::size_t* vcount, std::uint32_t** indices,
                              std::size_t* icount);
void truck_verts_free(float* ptr, std::size_t len);
void truck_indices_free(std::uint32_t* ptr, std::size_t len);

}  // extern "C"
