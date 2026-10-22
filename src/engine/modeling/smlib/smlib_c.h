#pragma once

// SMLib（NVIDIA USD BRep）的 C 门面。
//
// 为什么要有这一层：SMLib 的头文件只能在 C++17 下编译（C++20 起
// SmTreeVertex::operator== 的模板实例化会歧义），而 Tamias 的 kernel.h 是 C++23
// （std::span / std::expected）。所以：
//   - smlib_api.cpp  用 C++17 编，唯一 include SMLib 头；
//   - smlib_kernel.cpp 用 C++23 编，只 include 这个文件。
// 门面里的坐标一律是 Tamias 的 Y-up，SMLib 的 Z-up 只活在 smlib_api.cpp 里。
//
// 所有权：所有返回的 Brep / Profile / Mesh 都归调用方，用对应的 free_* 释放；
// 所有函数返回 0 表示成功，负数表示失败，细节用 last_error()。

#include <cstddef>
#include <cstdint>

namespace tamias::smlib_c {

struct Brep;
struct Profile;
struct Mesh;

struct Vec3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Bounds {
  double min[3] = {0.0, 0.0, 0.0};
  double max[3] = {0.0, 0.0, 0.0};
};

struct EdgeMeasure {
  std::uint64_t key = 0;
  Vec3 mid{};
  Vec3 dir{};
  bool has_dir = false;
  double length = 0.0;
};

struct MeshView {
  const float* positions = nullptr;  // 3 floats per vertex, Y-up
  const float* normals = nullptr;    // 3 floats per vertex
  std::size_t vertex_count = 0;
};

// 进程级初始化：创建 SMLib 的全局 context。可重复调用，只生效一次。
// 必须在任何其它函数之前调用；Tamias 在注册后端时调。
int init();
const char* last_error();

int make_rect_profile(double width, double height, Profile** out);
int make_circle_profile(double radius, Profile** out);
// points 是 Tamias Y-up、y=0 平面上的闭合点列。
int make_polygon_profile(const Vec3* points, std::size_t count, Profile** out);
void free_profile(Profile* profile);

int extrude(const Profile* profile, double depth, Brep** out);

// op: 0 = fuse, 1 = common, 2 = cut（对应 BooleanOp 的 Fuse/Common/Cut）。
int boolean(const Brep* a, const Brep* b, int op, Brep** out);
int transform(const Brep* body, Vec3 translation, Brep** out);
int cylinder(double radius, double height, Vec3 center, Vec3 axis, Brep** out);
int fillet(const Brep* body, const std::uint32_t* edge_ids, std::size_t count, double radius,
           Brep** out);
int chamfer(const Brep* body, const std::uint32_t* edge_ids, std::size_t count, double distance,
            Brep** out);

int edge_count(const Brep* body, std::size_t* out);
int measure_edges(const Brep* body, EdgeMeasure** out, std::size_t* count);
void free_edge_measures(EdgeMeasure* measures);

int tessellate(const Brep* body, double deflection, Mesh** out);
int mesh_view(const Mesh* mesh, MeshView* out);
void free_mesh(Mesh* mesh);

int bounds(const Brep* body, Bounds* out);

void free_brep(Brep* body);

}  // namespace tamias::smlib_c
