#pragma once

#include "engine/math/math.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace tamias {

// Discrete tessellation / draw lod. Not a scene-graph node: RecordCommands picks
// a level from screen error. See docs/MASSIVE-GEOMETRY.md G3.
enum class MeshLod : std::uint8_t {
  Box = 0,     // shared unit cube scaled to world AABB
  Coarse = 1,  // large deflection / far
  Work = 2,    // default editing mesh
  Close = 3,   // selected / section (reserved)
};

inline constexpr float kMeshLodSkipPixels = 1.f;
inline constexpr float kMeshLodBoxPixels = 4.f;
inline constexpr float kMeshLodCoarsePixels = 80.f;
inline constexpr float kMeshLodUpgradeScale = 1.25f;
inline constexpr float kMeshLodDowngradeScale = 0.8f;

inline constexpr double kMeshLodWorkDeflection = 0.05;
inline constexpr double kMeshLodCoarseDeflection = 0.6;
inline constexpr double kMeshLodCloseDeflection = 0.02;
inline constexpr double kMeshLodImportWorkDeflection = 0.1;
inline constexpr double kMeshLodImportCoarseDeflection = 1.6;

[[nodiscard]] inline double mesh_lod_deflection(MeshLod lod, bool cad_import) {
  switch (lod) {
    case MeshLod::Coarse:
      return cad_import ? kMeshLodImportCoarseDeflection : kMeshLodCoarseDeflection;
    case MeshLod::Close:
      return cad_import ? kMeshLodImportWorkDeflection : kMeshLodCloseDeflection;
    case MeshLod::Work:
    case MeshLod::Box:
    default:
      return cad_import ? kMeshLodImportWorkDeflection : kMeshLodWorkDeflection;
  }
}

// Projected AABB short edge in pixels (world min-extent / world-per-pixel).
[[nodiscard]] inline float projected_aabb_pixels(const Aabb& box, Vec3 eye, float fovy,
                                                 float framebuffer_height) {
  if (!box.valid()) {
    return 1.0e6f;
  }
  const Vec3 e = box.extent();
  float short_edge = std::min({e.x, e.y, e.z});
  if (short_edge < 1e-8f) {
    short_edge = std::max({e.x, e.y, e.z});
  }
  const float dist = std::max(length(box.center() - eye), 0.01f);
  const float h = std::max(framebuffer_height, 1.f);
  const float world_per_pixel = 2.f * std::tan(fovy * 0.5f) * dist / h;
  if (world_per_pixel <= 1e-12f) {
    return 1.0e6f;
  }
  return short_edge / world_per_pixel;
}

[[nodiscard]] inline MeshLod mesh_lod_from_pixels(float projected_px) {
  if (projected_px < kMeshLodBoxPixels) {
    return MeshLod::Box;
  }
  if (projected_px < kMeshLodCoarsePixels) {
    return MeshLod::Coarse;
  }
  return MeshLod::Work;
}

// Hysteresis: upgrade only when clearly finer, downgrade only when clearly coarser.
[[nodiscard]] inline MeshLod select_mesh_lod(float projected_px,
                                             std::optional<MeshLod> previous, bool selected,
                                             bool lines) {
  if (lines) {
    return MeshLod::Work;
  }
  if (selected) {
    return MeshLod::Work;
  }
  const MeshLod target = mesh_lod_from_pixels(projected_px);
  if (!previous.has_value() || *previous == target) {
    return target;
  }
  const MeshLod prev = *previous;
  const auto rank = [](MeshLod lod) { return static_cast<std::uint8_t>(lod); };
  if (rank(target) > rank(prev)) {
    if (prev == MeshLod::Box && projected_px >= kMeshLodBoxPixels * kMeshLodUpgradeScale) {
      return mesh_lod_from_pixels(projected_px);
    }
    if (prev == MeshLod::Coarse &&
        projected_px >= kMeshLodCoarsePixels * kMeshLodUpgradeScale) {
      return MeshLod::Work;
    }
    return prev;
  }
  if (prev == MeshLod::Work && projected_px <= kMeshLodCoarsePixels * kMeshLodDowngradeScale) {
    return mesh_lod_from_pixels(projected_px);
  }
  if (prev == MeshLod::Coarse && projected_px <= kMeshLodBoxPixels * kMeshLodDowngradeScale) {
    return MeshLod::Box;
  }
  return prev;
}

[[nodiscard]] inline bool mesh_lod_skip_draw(float projected_px, bool selected) {
  return !selected && projected_px < kMeshLodSkipPixels;
}

// Unit box from make_box_mesh(1,1,1) (extent 1, centered) → world AABB.
[[nodiscard]] inline Mat4 lod_box_world_matrix(const Aabb& world_bounds) {
  if (!world_bounds.valid()) {
    return Mat4::identity();
  }
  return translate(world_bounds.center()) * scale(world_bounds.extent());
}

}  // namespace tamias
