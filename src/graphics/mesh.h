#pragma once

#include "math/math.h"

#include <cstdint>
#include <vector>

namespace tamias {

struct Vertex {
  Vec3 position;
  Vec3 normal;
  Vec2 uv;
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

}  // namespace tamias
