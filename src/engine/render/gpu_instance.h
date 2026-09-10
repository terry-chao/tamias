#pragma once

#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

// 64-byte GPU instance record (G1). World is affine 3×4, column-major, matching Mat4's
// first three columns. Color/material stay unpacked in G1b until the shader path lands.
struct GpuInstance {
  float world[12]{};
  float color[4]{0.75f, 0.78f, 0.82f, 1.f};
  // x=roughness y=metallic z=selected w=has_uv (packed later)
  float material[4]{0.6f, 0.f, 0.f, 0.f};
  std::uint32_t node_id = 0;
  std::uint32_t flags = 0;
  std::uint32_t pad[2]{};
};

inline GpuInstance make_gpu_instance(const Mat4& world, Vec3 color, float opacity, float roughness,
                                     float metallic, bool selected, bool has_uv,
                                     std::uint32_t node_id) {
  GpuInstance inst{};
  // Mat4 is column-major; 3×4 = columns 0..2 of the affine part (translation in col 3).
  inst.world[0] = world(0, 0);
  inst.world[1] = world(1, 0);
  inst.world[2] = world(2, 0);
  inst.world[3] = world(0, 1);
  inst.world[4] = world(1, 1);
  inst.world[5] = world(2, 1);
  inst.world[6] = world(0, 2);
  inst.world[7] = world(1, 2);
  inst.world[8] = world(2, 2);
  inst.world[9] = world(0, 3);
  inst.world[10] = world(1, 3);
  inst.world[11] = world(2, 3);
  inst.color[0] = color.x;
  inst.color[1] = color.y;
  inst.color[2] = color.z;
  inst.color[3] = opacity;
  inst.material[0] = roughness;
  inst.material[1] = metallic;
  inst.material[2] = selected ? 1.f : 0.f;
  inst.material[3] = has_uv ? 1.f : 0.f;
  inst.node_id = node_id;
  return inst;
}

}  // namespace tamias
