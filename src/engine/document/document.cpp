#include "engine/document/document.h"

#include <cmath>

namespace tamias {

namespace {

constexpr std::uint32_t kMaterialTextureSize = 256;

std::uint8_t linear_to_srgb_u8(float linear) {
  const float x = std::clamp(linear, 0.f, 1.f);
  const float srgb = x <= 0.0031308f ? 12.92f * x
                                     : 1.055f * std::pow(x, 1.f / 2.4f) - 0.055f;
  return static_cast<std::uint8_t>(std::clamp(srgb, 0.f, 1.f) * 255.f);
}

float material_hash(std::uint32_t x, std::uint32_t y) {
  std::uint32_t n = x * 374761393u + y * 668265263u;
  n = (n ^ (n >> 13)) * 1274126177u;
  n ^= n >> 16;
  return static_cast<float>(n & 0xFFFFu) / 65535.0f;
}

template <typename Fn>
TextureAsset make_material_texture(Fn&& color_at) {
  TextureAsset texture;
  texture.width = kMaterialTextureSize;
  texture.height = kMaterialTextureSize;
  texture.rgba.resize(static_cast<std::size_t>(kMaterialTextureSize) *
                      kMaterialTextureSize * 4);
  for (std::uint32_t y = 0; y < kMaterialTextureSize; ++y) {
    for (std::uint32_t x = 0; x < kMaterialTextureSize; ++x) {
      const Vec3 linear = color_at(x, y);
      const std::size_t i =
          (static_cast<std::size_t>(y) * kMaterialTextureSize + x) * 4;
      texture.rgba[i + 0] = linear_to_srgb_u8(linear.x);
      texture.rgba[i + 1] = linear_to_srgb_u8(linear.y);
      texture.rgba[i + 2] = linear_to_srgb_u8(linear.z);
      texture.rgba[i + 3] = 255;
    }
  }
  return texture;
}

}  // namespace

void Document::seed_default_materials() {
  constexpr float kPi = 3.14159265358979f;
  constexpr float kInvSize = 1.f / static_cast<float>(kMaterialTextureSize);

  // 各预设使用 256×256 程序化贴图；纹理以 sRGB 编码写入，采样时由 GPU 转线性。
  const std::uint64_t default_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float coarse = material_hash(x >> 4, y >> 4);
        const float fine = material_hash(x, y);
        const float v = 0.70f + 0.035f * coarse + 0.025f * fine;
        return Vec3{v, v, v};
      })).id;

  const std::uint64_t concrete_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float coarse = material_hash(x >> 3, y >> 3);
        const float fine = material_hash(x, y);
        const float v = 0.58f + 0.10f * coarse + 0.06f * fine;
        return Vec3{v, v, v};
      })).id;

  const std::uint64_t steel_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float grain = material_hash(x, y);
        const float streak =
            0.5f +
            0.5f * std::sin(2.f * kPi * static_cast<float>(x) * 8.f * kInvSize);
        const float v = 0.52f + 0.06f * grain + 0.06f * streak;
        return Vec3{v, v, v};
      })).id;

  const std::uint64_t glass_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float coarse = material_hash(x >> 4, y >> 4);
        const float fine = material_hash(x, y);
        const float v = 0.76f + 0.03f * coarse + 0.025f * fine;
        return Vec3{0.96f * v, 0.99f * v, v};
      })).id;

  const std::uint64_t wood_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float px = static_cast<float>(x) * kInvSize;
        const float py = static_cast<float>(y) * kInvSize;
        const float grain =
            std::sin(2.f * kPi * px * 7.f + 1.8f * std::sin(2.f * kPi * py * 3.f)) +
            0.18f * std::sin(2.f * kPi * px * 17.f);
        const float fine = material_hash(x, y);
        const float shade = 0.80f + 0.16f * (0.5f + 0.5f * grain) + 0.025f * fine;
        return Vec3{0.58f * shade, 0.40f * shade, 0.24f * shade};
      })).id;

  const std::uint64_t plaster_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float coarse = material_hash(x >> 4, y >> 4);
        const float fine = material_hash(x, y);
        const float v = 0.86f + 0.04f * coarse + 0.03f * fine;
        return Vec3{v, v, v};
      })).id;

  auto seed = [this](std::string name, Vec3 color, float roughness, float metallic,
                     std::uint64_t albedo = 0) {
    Material m{};
    m.name = std::move(name);
    m.base_color = color;
    m.roughness = roughness;
    m.metallic = metallic;
    m.albedo_texture_id = albedo;
    add_material(std::move(m));
  };
  seed("Default", {0.75f, 0.78f, 0.82f}, 0.9f, 0.0f, default_tex_id);
  seed("Concrete", {0.62f, 0.62f, 0.60f}, 0.9f, 0.0f, concrete_tex_id);
  seed("Steel", {0.55f, 0.57f, 0.62f}, 0.4f, 0.9f, steel_tex_id);
  seed("Glass", {0.80f, 0.88f, 0.90f}, 0.1f, 0.0f, glass_tex_id);
  seed("Wood", {0.55f, 0.40f, 0.26f}, 0.7f, 0.0f, wood_tex_id);
  seed("Plaster", {0.92f, 0.90f, 0.85f}, 0.95f, 0.0f, plaster_tex_id);
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

std::vector<SceneDrawItem> Document::render_items(const Frustum* frustum) const {
  std::vector<SceneDrawItem> items;
  items.reserve(scene_.nodes().size());
  for (const auto& node : scene_.nodes()) {
    if (node.mesh_asset_id == 0) {
      continue;  // grouping / empty nodes carry no geometry
    }
    if (frustum != nullptr && !frustum->intersects(node.world_bounds)) {
      continue;
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
