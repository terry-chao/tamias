#pragma once

#include "engine/render/mesh_lod.h"

#include <cstdint>

namespace tamias {

// Per-geometry tessellation assets. 0 = not generated yet.
struct LodMeshSet {
  std::uint64_t coarse = 0;
  std::uint64_t work = 0;
  std::uint64_t close = 0;

  [[nodiscard]] std::uint64_t asset(MeshLod lod) const {
    switch (lod) {
      case MeshLod::Coarse:
        return coarse;
      case MeshLod::Close:
        return close;
      case MeshLod::Work:
        return work;
      case MeshLod::Box:
      default:
        return 0;
    }
  }

  void set_asset(MeshLod lod, std::uint64_t mesh_asset_id) {
    switch (lod) {
      case MeshLod::Coarse:
        coarse = mesh_asset_id;
        break;
      case MeshLod::Close:
        close = mesh_asset_id;
        break;
      case MeshLod::Work:
        work = mesh_asset_id;
        break;
      case MeshLod::Box:
        break;
    }
  }
};

}  // namespace tamias
