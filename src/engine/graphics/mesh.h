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

}  // namespace tamias
