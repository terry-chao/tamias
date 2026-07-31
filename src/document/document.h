#pragma once

#include "asset/mesh_asset.h"
#include "scene/scene.h"

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

  [[nodiscard]] Scene& scene() { return scene_; }
  [[nodiscard]] const Scene& scene() const { return scene_; }

  MeshAsset& add_mesh(MeshAsset asset) {
    asset.id = next_mesh_id_++;
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
};

}  // namespace tamias
