#pragma once

#include "engine/math/math.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace tamias {

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
  Vec3 color{1.f, 1.f, 1.f};
};

struct MeshCpu {
  std::vector<Vertex> vertices;
  std::vector<std::uint32_t> indices;
  Aabb bounds{};
  bool line_list = false;  // true：索引按 LineList 成对解释，不是三角面
};

inline void recompute_bounds(MeshCpu& mesh) {
  mesh.bounds = {};
  for (const auto& v : mesh.vertices) {
    mesh.bounds.expand(v.position);
  }
}

// True when any vertex color differs from the default white (1,1,1).
inline bool mesh_has_vertex_colors(const MeshCpu& mesh) {
  for (const auto& v : mesh.vertices) {
    if (std::fabs(v.color.x - 1.f) > 1e-4f || std::fabs(v.color.y - 1.f) > 1e-4f ||
        std::fabs(v.color.z - 1.f) > 1e-4f) {
      return true;
    }
  }
  return false;
}

// 轴对齐长方体网格（宽×高×深，中心在原点；X=宽，Y=高(up)，Z=深）。
inline MeshCpu make_box_mesh(float width, float height, float depth) {
  MeshCpu mesh;
  const float hw = width * 0.5f;
  const float hh = height * 0.5f;
  const float hd = depth * 0.5f;
  const Vec3 p[8] = {{-hw, -hh, -hd}, {hw, -hh, -hd}, {hw, hh, -hd}, {-hw, hh, -hd},
                     {-hw, -hh, hd},  {hw, -hh, hd},  {hw, hh, hd},  {-hw, hh, hd}};
  const int faces[6][4] = {{0, 3, 2, 1}, {4, 5, 6, 7}, {0, 1, 5, 4},
                           {3, 7, 6, 2}, {0, 4, 7, 3}, {1, 2, 6, 5}};
  const Vec3 normals[6] = {{0, 0, -1}, {0, 0, 1}, {0, -1, 0}, {0, 1, 0}, {-1, 0, 0}, {1, 0, 0}};
  const auto add_tri = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 n) {
    const auto base = static_cast<std::uint32_t>(mesh.vertices.size());
    Vertex va{};
    va.position = a;
    va.normal = n;
    Vertex vb = va;
    vb.position = b;
    Vertex vc = va;
    vc.position = c;
    mesh.vertices.push_back(va);
    mesh.vertices.push_back(vb);
    mesh.vertices.push_back(vc);
    mesh.indices.push_back(base);
    mesh.indices.push_back(base + 1);
    mesh.indices.push_back(base + 2);
  };
  for (int f = 0; f < 6; ++f) {
    add_tri(p[faces[f][0]], p[faces[f][1]], p[faces[f][2]], normals[f]);
    add_tri(p[faces[f][0]], p[faces[f][2]], p[faces[f][3]], normals[f]);
  }
  recompute_bounds(mesh);
  return mesh;
}

// 圆柱网格（半径 × 高，中心在原点，Y 轴为轴）。侧面径向法线，上下盖轴法线。
inline MeshCpu make_cylinder_mesh(float radius, float height, int segments = 24) {
  MeshCpu mesh;
  const float hh = height * 0.5f;
  const int n = std::max(3, segments);
  constexpr float kPi = 3.14159265358979f;

  const auto push = [&](Vec3 p, Vec3 normal) {
    Vertex v{};
    v.position = p;
    v.normal = normal;
    mesh.vertices.push_back(v);
    return static_cast<std::uint32_t>(mesh.vertices.size() - 1);
  };
  const auto tri = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
  };

  // 侧面（径向法线）
  std::vector<std::uint32_t> side_bottom;
  std::vector<std::uint32_t> side_top;
  side_bottom.reserve(static_cast<std::size_t>(n));
  side_top.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
    const Vec3 nrm{std::cos(a), 0.f, std::sin(a)};
    const Vec3 p{radius * std::cos(a), 0.f, radius * std::sin(a)};
    side_bottom.push_back(push({p.x, -hh, p.z}, nrm));
    side_top.push_back(push({p.x, hh, p.z}, nrm));
  }
  for (int i = 0; i < n; ++i) {
    const int j = (i + 1) % n;
    tri(side_bottom[static_cast<std::size_t>(i)], side_bottom[static_cast<std::size_t>(j)],
        side_top[static_cast<std::size_t>(j)]);
    tri(side_bottom[static_cast<std::size_t>(i)], side_top[static_cast<std::size_t>(j)],
        side_top[static_cast<std::size_t>(i)]);
  }

  // 顶部盖（向上法线）
  const auto top_center = push({0.f, hh, 0.f}, {0.f, 1.f, 0.f});
  std::vector<std::uint32_t> top_ring;
  top_ring.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
    top_ring.push_back(push({radius * std::cos(a), hh, radius * std::sin(a)}, {0.f, 1.f, 0.f}));
  }
  for (int i = 0; i < n; ++i) {
    tri(top_center, top_ring[static_cast<std::size_t>((i + 1) % n)],
        top_ring[static_cast<std::size_t>(i)]);
  }

  // 底部盖（向下法线）
  const auto bottom_center = push({0.f, -hh, 0.f}, {0.f, -1.f, 0.f});
  std::vector<std::uint32_t> bottom_ring;
  bottom_ring.reserve(static_cast<std::size_t>(n));
  for (int i = 0; i < n; ++i) {
    const float a = 2.f * kPi * static_cast<float>(i) / static_cast<float>(n);
    bottom_ring.push_back(
        push({radius * std::cos(a), -hh, radius * std::sin(a)}, {0.f, -1.f, 0.f}));
  }
  for (int i = 0; i < n; ++i) {
    tri(bottom_center, bottom_ring[static_cast<std::size_t>(i)],
        bottom_ring[static_cast<std::size_t>((i + 1) % n)]);
  }

  recompute_bounds(mesh);
  return mesh;
}

}  // namespace tamias
