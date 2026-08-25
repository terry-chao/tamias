#include "engine/document/document.h"

#include "entity/entity_grip.h"
#include "entity/kind_display_color.h"

#include <algorithm>
#include <cmath>

namespace tamias {

namespace {

constexpr std::uint32_t kMaterialTextureSize = 512;

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

int wrap_period(int x, int period) {
  int r = x % period;
  if (r < 0) {
    r += period;
  }
  return r;
}

float value_noise(float x, float y, int period) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float fx = x - static_cast<float>(x0);
  const float fy = y - static_cast<float>(y0);
  const float u = fx * fx * (3.f - 2.f * fx);
  const float v = fy * fy * (3.f - 2.f * fy);
  const float n00 = material_hash(static_cast<std::uint32_t>(wrap_period(x0, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0, period)));
  const float n10 = material_hash(static_cast<std::uint32_t>(wrap_period(x0 + 1, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0, period)));
  const float n01 = material_hash(static_cast<std::uint32_t>(wrap_period(x0, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0 + 1, period)));
  const float n11 = material_hash(static_cast<std::uint32_t>(wrap_period(x0 + 1, period)),
                                  static_cast<std::uint32_t>(wrap_period(y0 + 1, period)));
  const float nx0 = n00 + (n10 - n00) * u;
  const float nx1 = n01 + (n11 - n01) * u;
  return nx0 + (nx1 - nx0) * v;
}

float fbm_uv(float u, float v, int base_cells, int octaves) {
  float sum = 0.f;
  float amp = 1.f;
  float norm = 0.f;
  int cells = base_cells;
  for (int i = 0; i < octaves; ++i) {
    sum += amp * value_noise(u * static_cast<float>(cells), v * static_cast<float>(cells), cells);
    norm += amp;
    amp *= 0.5f;
    cells *= 2;
  }
  return sum / std::max(norm, 1e-6f);
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

  // 512×512 平滑 FBM，避免 8/16 像素色块；纹理以 sRGB 编码，采样时由 GPU 转线性。
  const std::uint64_t default_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float mottling = fbm_uv(u, v, 12, 5);
        const float grain = material_hash(x, y);
        const float t = 0.72f + 0.04f * mottling + 0.018f * grain;
        return Vec3{t, t, t};
      })).id;

  const std::uint64_t concrete_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float mottling = fbm_uv(u, v, 10, 6);
        const float aggregate = value_noise(u * 64.f, v * 64.f, 64);
        const float sand = material_hash(x, y);
        const float t = 0.58f + 0.10f * mottling + 0.05f * aggregate + 0.035f * sand;
        return Vec3{t * 1.08f, t * 1.00f, t * 0.90f};
      })).id;

  const std::uint64_t steel_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float grain = material_hash(x, y);
        const float warp = 0.12f * fbm_uv(u, v, 16, 4);
        const float streak =
            0.5f + 0.5f * std::sin(2.f * kPi * (u * 72.f + warp) + 0.35f * std::sin(2.f * kPi * v * 9.f));
        const float t = 0.54f + 0.035f * grain + 0.055f * streak;
        return Vec3{t, t, t};
      })).id;

  const std::uint64_t glass_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float mottling = fbm_uv(u, v, 8, 4);
        const float grain = material_hash(x, y);
        const float t = 0.78f + 0.018f * mottling + 0.012f * grain;
        return Vec3{0.96f * t, 0.99f * t, t};
      })).id;

  const std::uint64_t wood_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float warp = 0.28f * fbm_uv(u, v, 8, 5);
        const float grain =
            std::sin(2.f * kPi * (u * 28.f + warp) + 1.35f * std::sin(2.f * kPi * v * 6.f)) +
            0.16f * std::sin(2.f * kPi * (u * 54.f + 0.4f * warp));
        const float pores = material_hash(x, y);
        const float shade = 0.78f + 0.14f * (0.5f + 0.5f * grain) + 0.04f * pores;
        return Vec3{0.58f * shade, 0.39f * shade, 0.22f * shade};
      })).id;

  const std::uint64_t plaster_tex_id =
      add_texture(make_material_texture([](std::uint32_t x, std::uint32_t y) {
        const float u = (static_cast<float>(x) + 0.5f) * kInvSize;
        const float v = (static_cast<float>(y) + 0.5f) * kInvSize;
        const float mottling = fbm_uv(u, v, 14, 5);
        const float grain = material_hash(x, y);
        const float t = 0.88f + 0.025f * mottling + 0.02f * grain;
        return Vec3{t, t * 0.995f, t * 0.98f};
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
  seed("Default", {0.78f, 0.81f, 0.86f}, 0.9f, 0.0f, default_tex_id);
  seed("Concrete", {0.72f, 0.66f, 0.56f}, 0.92f, 0.0f, concrete_tex_id);
  seed("Steel", {0.55f, 0.57f, 0.62f}, 0.4f, 0.9f, steel_tex_id);
  seed("Glass", {0.80f, 0.88f, 0.90f}, 0.1f, 0.0f, glass_tex_id);
  seed("Wood", {0.55f, 0.40f, 0.26f}, 0.7f, 0.0f, wood_tex_id);
  seed("Plaster", {0.92f, 0.90f, 0.85f}, 0.95f, 0.0f, plaster_tex_id);
}

Storey& Document::add_storey(std::string name, double elevation) {
  SceneNode node{};
  node.name = name;
  SceneNode& stored_node = scene_.add_node(std::move(node));
  Storey storey{stored_node.id, std::move(name), elevation};
  Storey& stored = bim_.insert_storey(std::move(storey));
  if (bim_.active_storey_id() == 0) {
    bim_.set_active_storey_id(stored.id);
  }
  recompute_scene();
  mark_dirty();
  return stored;
}

Storey& Document::insert_storey(Storey storey) {
  SceneNode node{};
  node.id = storey.id;
  node.name = storey.name;
  scene_.insert_node(std::move(node));
  Storey& stored = bim_.insert_storey(std::move(storey));
  recompute_scene();
  mark_dirty();
  return stored;
}

void Document::remove_storey(std::uint64_t id) {
  const double elevation = bim_.storey_elevation(id);
  for (auto& [entity_id, entity_ptr] : entities_) {
    (void)entity_id;
    if (entity_ptr->location && entity_ptr->location->storey_id() == id) {
      entity_ptr->location->set_storey_id(0);
      entity_ptr->location->set_elevation_offset(
          entity_ptr->location->elevation_offset() + elevation);
      entity_ptr->sync_from_location(0.0);
      scene_.set_parent(entity_ptr->id, 0);
      scene_.set_transform(entity_ptr->id, entity_ptr->local_transform);
    }
  }
  bim_.remove_storey(id);
  scene_.remove_node(id);
  recompute_scene();
  mark_dirty();
}

void Document::set_active_storey(std::uint64_t id) {
  bim_.set_active_storey_id(id);
  mark_dirty();
}

void Document::assign_active_storey(Entity& entity) {
  if (!entity.location) {
    return;
  }
  const std::uint64_t storey_id = bim_.active_storey_id();
  const double world_elevation =
      static_cast<double>(entity.local_transform(1, 3));
  entity.location->set_storey_id(storey_id);
  entity.location->set_elevation_offset(
      world_elevation - bim_.storey_elevation(storey_id));
  entity.sync_from_location(bim_.storey_elevation(storey_id));
}

bool Document::sync_entity_location(std::uint64_t entity_id) {
  Entity* target = entity(entity_id);
  if (target == nullptr || !target->location) {
    return false;
  }
  const std::uint64_t storey_id = target->location->storey_id();
  target->sync_from_location(bim_.storey_elevation(storey_id));
  scene_.set_transform(entity_id, target->local_transform);
  scene_.set_parent(entity_id, bim_.find_storey(storey_id) != nullptr ? storey_id : 0);
  recompute_scene();
  mark_dirty();
  return true;
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
  if (entity->location && bim_.find_storey(entity->location->storey_id()) != nullptr) {
    node.parent = entity->location->storey_id();
  }
  SceneNode& stored_node = scene_.add_node(std::move(node));
  entity->id = stored_node.id;  // entity id == scene node id

  Entity* raw = entity.get();
  entities_[entity->id] = std::move(entity);
  if (raw->grips.empty()) {
    sync_entity_grips(*raw);
  }
  if (raw->material_id == 0) {
    const char* preset = nullptr;
    switch (raw->kind()) {
      case EntityKind::Wall:
      case EntityKind::Beam:
      case EntityKind::Column:
      case EntityKind::Slab:
        preset = "Concrete";
        break;
      default:
        break;
    }
    if (preset != nullptr) {
      for (const auto& [id, mat] : materials_) {
        if (mat.name == preset) {
          raw->material_id = id;
          break;
        }
      }
    }
  }
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
  bim_.remove_involving(id);
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
  if (entity->location && bim_.find_storey(entity->location->storey_id()) != nullptr) {
    node.parent = entity->location->storey_id();
  }
  scene_.insert_node(std::move(node));

  entities_[id] = std::move(entity);
  if (Entity* raw = entities_[id].get(); raw != nullptr && raw->grips.empty()) {
    sync_entity_grips(*raw);
  }
  recompute_scene();
  mark_dirty();
}

void Document::insert_entity(std::unique_ptr<Entity> entity) {
  const std::uint64_t id = entity->id;
  entities_[id] = std::move(entity);
  if (Entity* raw = entities_[id].get(); raw != nullptr && raw->grips.empty()) {
    sync_entity_grips(*raw);
  }
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
    item.bounds = node.world_bounds;
    item.color = node.color;
    item.category_color = kImportDisplayColor;
    item.selected = node.selected;
    if (const Entity* e = entity(node.id); e != nullptr) {
      item.category_color = display_color_for_kind(e->kind());
      if (e->material_id != 0) {
        if (const Material* m = material(e->material_id)) {
          item.color = m->base_color;
          item.roughness = m->roughness;
          item.metallic = m->metallic;
          item.albedo_texture_id = m->albedo_texture_id;
          item.normal_texture_id = m->normal_texture_id;
        }
      } else if (e->is_sketch_entity()) {
        // 草图用青色，与混凝土灰、选中蓝分开。
        item.color = display_color_for_kind(e->kind());
        item.lines = true;
      }
    }
    if (const MeshAsset* asset = mesh(node.mesh_asset_id); asset != nullptr && asset->cpu.line_list) {
      item.lines = true;
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
