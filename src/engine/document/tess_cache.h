#pragma once

#include "engine/render/lod_mesh_set.h"
#include "engine/render/mesh_lod.h"

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

namespace tamias {

// Maps geometry identity (SceneNode.mesh_asset_id) to per-lod MeshAsset ids.
class TessCache {
 public:
  void bind(std::uint64_t geometry_id, MeshLod lod, std::uint64_t mesh_asset_id) {
    if (geometry_id == 0 || lod == MeshLod::Box) {
      return;
    }
    sets_[geometry_id].set_asset(lod, mesh_asset_id);
    clear_pending(geometry_id, lod);
  }

  [[nodiscard]] std::uint64_t mesh_for(std::uint64_t geometry_id, MeshLod lod) const {
    const auto it = sets_.find(geometry_id);
    if (it == sets_.end()) {
      return lod == MeshLod::Work ? geometry_id : 0;
    }
    const std::uint64_t id = it->second.asset(lod);
    if (id != 0) {
      return id;
    }
    return lod == MeshLod::Work ? geometry_id : 0;
  }

  [[nodiscard]] LodMeshSet set_for(std::uint64_t geometry_id) const {
    const auto it = sets_.find(geometry_id);
    if (it == sets_.end()) {
      LodMeshSet set{};
      set.work = geometry_id;
      return set;
    }
    LodMeshSet set = it->second;
    if (set.work == 0) {
      set.work = geometry_id;
    }
    return set;
  }

  [[nodiscard]] std::unordered_map<std::uint64_t, LodMeshSet> snapshot() const {
    std::unordered_map<std::uint64_t, LodMeshSet> out = sets_;
    for (auto& [geom, set] : out) {
      if (set.work == 0) {
        set.work = geom;
      }
    }
    return out;
  }

  void invalidate(std::uint64_t geometry_id) {
    sets_.erase(geometry_id);
    pending_.erase(pack(geometry_id, MeshLod::Coarse));
    pending_.erase(pack(geometry_id, MeshLod::Work));
    pending_.erase(pack(geometry_id, MeshLod::Close));
  }

  void clear() {
    sets_.clear();
    pending_.clear();
  }

  [[nodiscard]] bool uses_asset(std::uint64_t mesh_asset_id) const {
    if (mesh_asset_id == 0) {
      return false;
    }
    for (const auto& [geom, set] : sets_) {
      if (geom == mesh_asset_id || set.coarse == mesh_asset_id || set.work == mesh_asset_id ||
          set.close == mesh_asset_id) {
        return true;
      }
    }
    return false;
  }

  [[nodiscard]] bool pending(std::uint64_t geometry_id, MeshLod lod) const {
    return pending_.count(pack(geometry_id, lod)) != 0;
  }

  void mark_pending(std::uint64_t geometry_id, MeshLod lod) {
    pending_.insert(pack(geometry_id, lod));
  }

  void clear_pending(std::uint64_t geometry_id, MeshLod lod) {
    pending_.erase(pack(geometry_id, lod));
  }

  [[nodiscard]] std::uint32_t pending_count() const {
    return static_cast<std::uint32_t>(pending_.size());
  }

 private:
  [[nodiscard]] static std::uint64_t pack(std::uint64_t geometry_id, MeshLod lod) {
    return (geometry_id << 8) | static_cast<std::uint64_t>(lod);
  }

  std::unordered_map<std::uint64_t, LodMeshSet> sets_;
  std::unordered_set<std::uint64_t> pending_;
};

}  // namespace tamias
