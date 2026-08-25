#pragma once

#include "bim/bim_model.h"
#include "engine/document/mesh_asset.h"
#include "engine/document/scene.h"
#include "entity/entity.h"
#include "engine/render/render_types.h"
#include "engine/render/material.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace tamias {

class Document {
 public:
  explicit Document(std::string name = "Untitled") : name_(std::move(name)) {
    seed_default_materials();
  }

  [[nodiscard]] const std::string& name() const { return name_; }
  void set_name(std::string name) { name_ = std::move(name); }
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  void set_path(std::filesystem::path path) { path_ = std::move(path); }

  [[nodiscard]] bool dirty() const { return dirty_; }
  void mark_dirty() { dirty_ = true; }
  void clear_dirty() { dirty_ = false; }

  [[nodiscard]] Scene& scene() { return scene_; }
  [[nodiscard]] const Scene& scene() const { return scene_; }

  [[nodiscard]] BimModel& bim() { return bim_; }
  [[nodiscard]] const BimModel& bim() const { return bim_; }

  MeshAsset& add_mesh(MeshAsset asset) {
    asset.id = next_mesh_id_++;
    auto& stored = meshes_[asset.id];
    stored = std::move(asset);
    return stored;
  }

  // Insert a mesh keeping its id (used by document load / undo redo).
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

  // ===== 材质库（共享命名材质，实体存 material_id 引用）=====

  Material& add_material(Material material) {
    material.id = next_material_id_++;
    auto& stored = materials_[material.id];
    stored = std::move(material);
    return stored;
  }

  // 插入保留 id 的材质（供 load / undo-redo 用）。
  Material& insert_material(Material material) {
    if (material.id == 0) {
      material.id = next_material_id_++;
    } else {
      next_material_id_ = std::max(next_material_id_, material.id + 1);
    }
    auto& stored = materials_[material.id];
    stored = std::move(material);
    return stored;
  }

  Material* material(std::uint64_t id) {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
  }
  const Material* material(std::uint64_t id) const {
    auto it = materials_.find(id);
    return it == materials_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] const std::unordered_map<std::uint64_t, Material>& materials() const {
    return materials_;
  }
  [[nodiscard]] std::uint64_t next_material_id() const { return next_material_id_; }
  void set_next_material_id(std::uint64_t id) {
    next_material_id_ = std::max<std::uint64_t>(1, id);
  }

  // ===== 纹理资产（RGBA8 字节，解码在 app 层）=====

  TextureAsset& add_texture(TextureAsset asset) {
    asset.id = next_texture_id_++;
    auto& stored = textures_[asset.id];
    stored = std::move(asset);
    return stored;
  }
  TextureAsset& insert_texture(TextureAsset asset) {
    if (asset.id == 0) {
      asset.id = next_texture_id_++;
    } else {
      next_texture_id_ = std::max(next_texture_id_, asset.id + 1);
    }
    auto& stored = textures_[asset.id];
    stored = std::move(asset);
    return stored;
  }
  const TextureAsset* texture(std::uint64_t id) const {
    auto it = textures_.find(id);
    return it == textures_.end() ? nullptr : &it->second;
  }
  [[nodiscard]] const std::unordered_map<std::uint64_t, TextureAsset>& textures() const {
    return textures_;
  }
  [[nodiscard]] std::uint64_t next_texture_id() const { return next_texture_id_; }
  void set_next_texture_id(std::uint64_t id) {
    next_texture_id_ = std::max<std::uint64_t>(1, id);
  }

  void clear_content() {
    scene_.clear();
    meshes_.clear();
    entities_.clear();
    bim_.clear();
    next_mesh_id_ = 1;
  }

  // 删除指定网格资产（供命令撤销用）。
  void remove_mesh(std::uint64_t id) { meshes_.erase(id); }

  // ===== 领域实体 API（封装 SceneNode，command/app 不直接碰节点）=====

  // 添加已求值的参数化实体。Document 不关心具体 Entity 子类；
  // 几何由调用方经 Entity::createGeom 求得，然后统一交给本方法。
  Entity* add_entity(std::unique_ptr<Entity> entity, MeshCpu mesh);

  Entity* entity(std::uint64_t id) {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
  }
  const Entity* entity(std::uint64_t id) const {
    auto it = entities_.find(id);
    return it == entities_.end() ? nullptr : it->second.get();
  }

  // 反查：给定 mesh 资产 id，返回喂给它的实体（选中节点 → 实体）。
  Entity* entity_for_mesh(std::uint64_t mesh_asset_id) {
    for (auto& [unused, e] : entities_) {
      (void)unused;
      if (e->mesh_asset_id == mesh_asset_id) {
        return e.get();
      }
    }
    return nullptr;
  }
  const Entity* entity_for_mesh(std::uint64_t mesh_asset_id) const {
    for (const auto& [unused, e] : entities_) {
      (void)unused;
      if (e->mesh_asset_id == mesh_asset_id) {
        return e.get();
      }
    }
    return nullptr;
  }

  [[nodiscard]] const std::unordered_map<std::uint64_t, std::unique_ptr<Entity>>& entities() const {
    return entities_;
  }

  // 删除实体（连带其场景节点 + 网格），供命令撤销用。
  void remove_entity(std::uint64_t id);

  // 重插实体（连带网格 + 场景节点），供命令 redo 用。
  void insert_entity(std::unique_ptr<Entity> entity, MeshAsset mesh);

  // 只存实体（其 node + mesh 已存在），供 load 用。
  void insert_entity(std::unique_ptr<Entity> entity);

  // ===== 选择（内部改 SceneNode.selected）=====
  void select(std::uint64_t id) {
    if (SceneNode* node = scene_.find(id)) {
      node->selected = true;
      scene_.bump_generation();
      scene_.mark_dirty(id);
    }
  }
  void deselect(std::uint64_t id) {
    if (SceneNode* node = scene_.find(id)) {
      node->selected = false;
      scene_.bump_generation();
      scene_.mark_dirty(id);
    }
  }
  void clear_selection() { scene_.clear_selection(); }
  [[nodiscard]] std::vector<std::uint64_t> selected_ids() const { return scene_.selected_ids(); }

  // 语义节点变化但没走 Scene mutator（例如实体 material_id 引用变更）时，
  // 通知渲染侧增量同步：标记该节点脏并递增代次。
  void mark_scene_dirty(std::uint64_t node_id) {
    scene_.bump_generation();
    scene_.mark_dirty(node_id);
  }
  Entity* selected_entity() {
    const SceneNode* node = scene_.selected_node();
    return node ? entity(node->id) : nullptr;
  }
  const Entity* selected_entity() const {
    const SceneNode* node = scene_.selected_node();
    return node ? entity(node->id) : nullptr;
  }
  // 选中对象的网格（实体或导入网格都适用）。
  const MeshAsset* selected_mesh() const {
    const SceneNode* node = scene_.selected_node();
    if (node == nullptr || node->mesh_asset_id == 0) {
      return nullptr;
    }
    return mesh(node->mesh_asset_id);
  }

  // ===== 渲染快照（app 不再遍历 SceneNode）=====
  // 传入 frustum 时丢掉世界包围盒完全在视锥外的叶子；nullptr 保持全量清单。
  [[nodiscard]] std::vector<SceneDrawItem> render_items(const Frustum* frustum = nullptr) const;

  // ===== 导入网格（无实体，如 STEP/OBJ/glTF）=====
  std::uint64_t add_import_mesh(std::string name, MeshCpu mesh, Mat4 transform, Vec3 color);

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
  void seed_default_materials();

  std::string name_;
  std::filesystem::path path_;
  Scene scene_;
  BimModel bim_;
  std::unordered_map<std::uint64_t, MeshAsset> meshes_;
  std::unordered_map<std::uint64_t, std::unique_ptr<Entity>> entities_;
  std::unordered_map<std::uint64_t, Material> materials_;
  std::unordered_map<std::uint64_t, TextureAsset> textures_;
  std::uint64_t next_mesh_id_ = 1;
  std::uint64_t next_material_id_ = 1;
  std::uint64_t next_texture_id_ = 1;
  bool dirty_ = false;
};

}  // namespace tamias
