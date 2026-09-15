#pragma once

#include <cstdint>

namespace tamias {

struct RenderFrameStats {
  std::uint32_t draws = 0;
  std::uint32_t triangles = 0;
  std::uint64_t gpu_mesh_bytes = 0;
  std::uint32_t pending_tessellate = 0;
  std::uint32_t lod_requests = 0;
};

}  // namespace tamias
