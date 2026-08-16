#include "engine/document/document.h"

namespace tamias {

void Document::seed_default_materials() {
  auto seed = [this](std::string name, Vec3 color, float roughness, float metallic) {
    Material m{};
    m.name = std::move(name);
    m.base_color = color;
    m.roughness = roughness;
    m.metallic = metallic;
    add_material(std::move(m));
  };
  seed("Default", {0.75f, 0.78f, 0.82f}, 0.9f, 0.0f);
  seed("Concrete", {0.62f, 0.62f, 0.60f}, 0.9f, 0.0f);
  seed("Steel", {0.55f, 0.57f, 0.62f}, 0.4f, 0.9f);
  seed("Glass", {0.80f, 0.88f, 0.90f}, 0.1f, 0.0f);
  seed("Wood", {0.55f, 0.40f, 0.26f}, 0.7f, 0.0f);
  seed("Plaster", {0.92f, 0.90f, 0.85f}, 0.95f, 0.0f);
}

// 只接收已求值的实体 + 几何，不做造型（造型在 Entity::createGeom，见 entity.cpp）。
Entity* Document::add_entity(std::unique_ptr<Entity> entity, MeshCpu mesh) {
  MeshAsset asset{};
  asset.name = entity->name;
  asset.cpu = std::move(mesh);
  MeshAsset& stored_mesh = add_mesh(std::move(asset));
  entity->mesh_asset_id = stored_mesh.id;

  SceneNode node{};
  node.name = entity->name;
  node.mesh_asset_id = entity->mesh_asset_id;
  node.local_transform = entity->local_transform;
  SceneNode& stored_node = scene_.add_node(std::move(node));
  entity->id = stored_node.id;  // entity id == scene node id

  Entity* raw = entity.get();
  entities_[entity->id] = std::move(entity);
  recompute_scene();
  mark_dirty();
  return raw;
}

Entity* Document::add_wall(WallEntity wall, MeshCpu mesh) {
  return add_entity(std::make_unique<WallEntity>(std::move(wall)), std::move(mesh));
}

Entity* Document::add_box(BoxEntity box, MeshCpu mesh) {
  return add_entity(std::make_unique<BoxEntity>(std::move(box)), std::move(mesh));
}

Entity* Document::add_cylinder(CylinderEntity cylinder, MeshCpu mesh) {
  return add_entity(std::make_unique<CylinderEntity>(std::move(cylinder)), std::move(mesh));
}

void Document::remove_entity(std::uint64_t id) {
  auto it = entities_.find(id);
  if (it == entities_.end()) {
    return;
  }
  const std::uint64_t mesh_id = it->second->mesh_asset_id;
  entities_.erase(it);
  scene_.remove_node(id);
  remove_mesh(mesh_id);
  recompute_scene();
  mark_dirty();
}

void Document::insert_entity(std::unique_ptr<Entity> entity, MeshAsset mesh) {
  const std::uint64_t id = entity->id;
  const std::uint64_t mesh_id = entity->mesh_asset_id;
  insert_mesh(std::move(mesh));

  SceneNode node{};
  node.id = id;
  node.name = entity->name;
  node.mesh_asset_id = mesh_id;
  node.local_transform = entity->local_transform;
  scene_.insert_node(std::move(node));

  entities_[id] = std::move(entity);
  recompute_scene();
  mark_dirty();
}

std::vector<SceneDrawItem> Document::render_items() const {
  std::vector<SceneDrawItem> items;
  items.reserve(scene_.nodes().size());
  for (const auto& node : scene_.nodes()) {
    if (node.mesh_asset_id == 0) {
      continue;  // grouping / empty nodes carry no geometry
    }
    SceneDrawItem item{};
    item.node_id = node.id;
    item.mesh_asset_id = node.mesh_asset_id;
    item.transform = node.world_transform;
    item.color = node.color;
    item.selected = node.selected;
    // 实体带材质：解析 material_id → base_color/rough/metallic/纹理 id；否则回退节点色（导入网格）。
    if (const Entity* e = entity(node.id); e != nullptr && e->material_id != 0) {
      if (const Material* m = material(e->material_id)) {
        item.color = m->base_color;
        item.roughness = m->roughness;
        item.metallic = m->metallic;
        item.albedo_texture_id = m->albedo_texture_id;
        item.normal_texture_id = m->normal_texture_id;
      }
    }
    items.push_back(item);
  }
  return items;
}

std::uint64_t Document::add_import_mesh(std::string name, MeshCpu mesh, Mat4 transform,
                                        Vec3 color) {
  MeshAsset asset{};
  asset.name = std::move(name);
  asset.cpu = std::move(mesh);
  MeshAsset& stored_mesh = add_mesh(std::move(asset));

  SceneNode node{};
  node.name = stored_mesh.name;
  node.mesh_asset_id = stored_mesh.id;
  node.local_transform = transform;
  node.color = color;
  scene_.add_node(std::move(node));
  recompute_scene();
  mark_dirty();
  return stored_mesh.id;
}

}  // namespace tamias
