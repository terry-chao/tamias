#pragma once

#include "engine/math/math.h"

#include <algorithm>
#include <cmath>

namespace tamias {

// 与 shaders/grid.frag.hlsl 一致：次网格 1、主网格 5。
inline constexpr float kGridMinorSpacing = 1.f;
inline constexpr float kGridMajorSpacing = 5.f;
// 屏幕空间捕捉半径（逻辑像素）；世界半径不超过间距的该比例，避免格子中心也被吸走。
inline constexpr float kGridSnapPixels = 12.f;
inline constexpr float kGridSnapRadiusFactor = 0.4f;

// 将点吸附到 XZ 平面网格线交点（Y 保持不变）。
inline Vec3 snap_to_grid_xz(Vec3 p, float spacing = kGridMinorSpacing) {
  if (!(spacing > 0.f)) {
    return p;
  }
  return {std::round(p.x / spacing) * spacing, p.y, std::round(p.z / spacing) * spacing};
}

// 由视距与视野算出 XZ 捕捉半径，并限制在半格以内。
inline float grid_snap_world_radius(float eye_distance, float fovy, float viewport_height,
                                    float spacing = kGridMinorSpacing) {
  const float h = std::max(viewport_height, 1.f);
  const float dist = std::max(eye_distance, 0.01f);
  const float world_per_pixel = 2.f * std::tan(fovy * 0.5f) * dist / h;
  return std::min(kGridSnapPixels * world_per_pixel, spacing * kGridSnapRadiusFactor);
}

// 落在交点 radius 内则吸附，否则保持原位。radius 为 XZ 平面距离。
inline Vec3 snap_to_grid_xz_if_near(Vec3 p, float radius, float spacing = kGridMinorSpacing) {
  if (!(radius > 0.f)) {
    return p;
  }
  const Vec3 snapped = snap_to_grid_xz(p, spacing);
  const float dx = p.x - snapped.x;
  const float dz = p.z - snapped.z;
  if (dx * dx + dz * dz <= radius * radius) {
    return snapped;
  }
  return p;
}

inline bool is_on_grid_xz(Vec3 p, float spacing = kGridMinorSpacing, float eps = 1e-4f) {
  const Vec3 snapped = snap_to_grid_xz(p, spacing);
  const float dx = p.x - snapped.x;
  const float dz = p.z - snapped.z;
  return dx * dx + dz * dz <= eps * eps;
}

}  // namespace tamias
