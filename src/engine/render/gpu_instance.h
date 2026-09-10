#pragma once

#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

// GPU instance record (G1). Affine world as three float4 rows so the shader can
// `dot(row, float4(pos,1))` without constructing a float4x4 (HLSL ctor order).
// Color / roughness / metallic / selected live here so they do not split batches.
struct GpuInstance {
  float row0[4]{1.f, 0.f, 0.f, 0.f};
  float row1[4]{0.f, 1.f, 0.f, 0.f};
  float row2[4]{0.f, 0.f, 1.f, 0.f};
  float color[4]{0.75f, 0.78f, 0.82f, 1.f};
  // x=roughness y=metallic z=selected w=unused (has_uv stays batch-level)
  float material[4]{0.6f, 0.f, 0.f, 0.f};
};

static_assert(sizeof(GpuInstance) == 80);

inline GpuInstance make_gpu_instance(const Mat4& world, Vec3 color, float opacity, float roughness,
                                     float metallic, bool selected) {
  GpuInstance inst{};
  inst.row0[0] = world(0, 0);
  inst.row0[1] = world(0, 1);
  inst.row0[2] = world(0, 2);
  inst.row0[3] = world(0, 3);
  inst.row1[0] = world(1, 0);
  inst.row1[1] = world(1, 1);
  inst.row1[2] = world(1, 2);
  inst.row1[3] = world(1, 3);
  inst.row2[0] = world(2, 0);
  inst.row2[1] = world(2, 1);
  inst.row2[2] = world(2, 2);
  inst.row2[3] = world(2, 3);
  inst.color[0] = color.x;
  inst.color[1] = color.y;
  inst.color[2] = color.z;
  inst.color[3] = opacity;
  inst.material[0] = roughness;
  inst.material[1] = metallic;
  inst.material[2] = selected ? 1.f : 0.f;
  inst.material[3] = 0.f;
  return inst;
}

inline Mat4 gpu_instance_world(const GpuInstance& inst) {
  Mat4 world = Mat4::identity();
  world(0, 0) = inst.row0[0];
  world(0, 1) = inst.row0[1];
  world(0, 2) = inst.row0[2];
  world(0, 3) = inst.row0[3];
  world(1, 0) = inst.row1[0];
  world(1, 1) = inst.row1[1];
  world(1, 2) = inst.row1[2];
  world(1, 3) = inst.row1[3];
  world(2, 0) = inst.row2[0];
  world(2, 1) = inst.row2[1];
  world(2, 2) = inst.row2[2];
  world(2, 3) = inst.row2[3];
  return world;
}

}  // namespace tamias
