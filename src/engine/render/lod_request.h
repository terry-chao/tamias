#pragma once

#include "engine/render/mesh_lod.h"

#include <cstdint>
#include <functional>

namespace tamias {

struct LodRequest {
  std::uint64_t geometry_id = 0;
  MeshLod lod = MeshLod::Work;

  [[nodiscard]] bool operator==(const LodRequest& o) const {
    return geometry_id == o.geometry_id && lod == o.lod;
  }
};

struct LodRequestHash {
  std::size_t operator()(const LodRequest& r) const {
    std::size_t h = std::hash<std::uint64_t>{}(r.geometry_id);
    h ^= std::hash<std::uint8_t>{}(static_cast<std::uint8_t>(r.lod)) + 0x9e3779b9 + (h << 6) +
         (h >> 2);
    return h;
  }
};

}  // namespace tamias
