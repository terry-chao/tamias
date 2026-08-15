#pragma once

#include "asset/mesh_asset.h"
#include "scene/scene.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace tamias {

class Document {
 public:
  explicit Document(std::string name = "Untitled") : name_(std::move(name)) {}

  [[nodiscard]] const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  void set_path(std::filesystem::path path) { path_ = std::move(path); }

  [[nodiscard]] bool dirty() const { return dirty_; }
  void mark_dirty() { dirty_ = true; }
  void clear_dirty() { dirty_ = false; }

  [[nodiscard]] Scene& scene() { return scene_; }
  [[nodiscard]] const Scene& scene() const { return scene_; }

  MeshAsset& add_mesh(MeshAsset asset) {
    asset.id = next_mesh_id_++;
    auto& stored = meshes_[asset.id];
    stored = std::move(asset);
    return stored;
  }

  // Insert a mesh keeping its id (used by document load / history restore).
  MeshAsset& insert_mesh(MeshAsset asset) {
    if (asset.id == 0) {
      asset.id = next_mesh_id_++;
    } else {
      next_mesh_id_ = std::max(next_mesh_id_, asset.id + 1);
    }
    auto& stored = meshes_[asset.id];
    stored = std::move(asset);
    return stored;
  }

  MeshAsset* mesh(std::uint64_t id) {
    auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
  }

  const MeshAsset* mesh(std::uint64_t id) const {
    auto it = meshes_.find(id);
    return it == meshes_.end() ? nullptr : &it->second;
  }

  [[nodiscard]] std::unordered_map<std::uint64_t, MeshAsset>& meshes() { return meshes_; }
  [[nodiscard]] const std::unordered_map<std::uint64_t, MeshAsset>& meshes() const {
    return meshes_;
  }

  [[nodiscard]] std::uint64_t next_mesh_id() const { return next_mesh_id_; }
  void set_next_mesh_id(std::uint64_t id) { next_mesh_id_ = std::max<std::uint64_t>(1, id); }

  void clear_content() {
    scene_.clear();
    meshes_.clear();
    next_mesh_id_ = 1;
  }

  // Sync each node's local bounds from its mesh, then recompute the whole scene's
  // world transforms + world bounds. Call after load or any transform/parent edit.
  void recompute_scene() {
    for (auto& node : scene_.nodes()) {
      if (node.mesh_asset_id != 0) {
        if (const MeshAsset* m = mesh(node.mesh_asset_id)) {
          node.local_bounds = m->cpu.bounds;
        }
      }
    }
    scene_.recompute_world();
  }

  [[nodiscard]] Aabb bounds() const {
    Aabb box{};
    bool any = false;
    for (const auto& node : scene_.nodes()) {
      if (!node.world_bounds.valid()) {
        continue;
      }
      if (!any) {
        box = node.world_bounds;
        any = true;
      } else {
        box.expand(node.world_bounds.min);
        box.expand(node.world_bounds.max);
      }
    }
    return box;
  }

 private:
  std::string name_;
  std::filesystem::path path_;
  Scene scene_;
  std::unordered_map<std::uint64_t, MeshAsset> meshes_;
  std::uint64_t next_mesh_id_ = 1;
  bool dirty_ = false;
};

}  // namespace tamias
