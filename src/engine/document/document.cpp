#include "engine/document/document.h"

#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "bim/wall_join.h"
#include "engine/math/math.h"
#include "engine/modeling/occt_geom_builder.h"
#include "engine/render/builtin_textures.h"
#include "engine/render/mesh_lod.h"
#include "entity/entity_grip.h"
#include "entity/kind_display_color.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace tamias {

Document::~Document() {
  TessWorker::instance().cancel_all();
  TessWorker::instance().wait_idle();
}

void Document::seed_default_materials() {
  auto add_named = [this](std::string_view key) { return add_texture(make_builtin_texture(key)).id; };
  const std::uint64_t default_tex_id = add_named(kBuiltinDefaultAlbedo);
  const std::uint64_t concrete_tex_id = add_named(kBuiltinConcreteAlbedo);
  const std::uint64_t steel_tex_id = add_named(kBuiltinSteelAlbedo);
  const std::uint64_t wood_tex_id = add_named(kBuiltinWoodAlbedo);
  const std::uint64_t plaster_tex_id = add_named(kBuiltinPlasterAlbedo);
  const std::uint64_t default_n_id = add_named(kBuiltinDefaultNormal);
  const std::uint64_t concrete_n_id = add_named(kBuiltinConcreteNormal);
  const std::uint64_t wood_n_id = add_named(kBuiltinWoodNormal);
  const std::uint64_t steel_n_id = add_named(kBuiltinSteelNormal);
  const std::uint64_t plaster_n_id = add_named(kBuiltinPlasterNormal);
  const std::uint64_t glass_n_id = add_named(kBuiltinGlassNormal);

  auto seed = [this](std::string name, Vec3 color, float roughness, float metallic,
                     std::uint64_t albedo = 0, std::uint64_t normal = 0, float opacity = 1.f) {
    Material m{};
    m.name = std::move(name);
    m.base_color = color;
    m.roughness = roughness;
    m.metallic = metallic;
    m.opacity = opacity;
    m.albedo_texture_id = albedo;
    m.normal_texture_id = normal;
    add_material(std::move(m));
  };
  seed("Default", {0.78f, 0.81f, 0.86f}, 0.9f, 0.0f, default_tex_id, default_n_id);
  seed("Concrete", {0.72f, 0.66f, 0.56f}, 0.92f, 0.0f, concrete_tex_id, concrete_n_id);
  seed("Steel", {0.55f, 0.57f, 0.62f}, 0.4f, 0.9f, steel_tex_id, steel_n_id);
  seed("Glass", {0.52f, 0.76f, 0.84f}, 0.05f, 0.0f, 0, glass_n_id, 0.16f);
  seed("Wood", {0.55f, 0.40f, 0.26f}, 0.7f, 0.0f, wood_tex_id, wood_n_id);
  seed("Plaster", {0.92f, 0.90f, 0.85f}, 0.95f, 0.0f, plaster_tex_id, plaster_n_id);
}

std::uint32_t Document::material_user_count(std::uint64_t id) const {
  if (id == 0) {
    return 0;
  }
  std::uint32_t n = 0;
  for (const auto& [unused, entity] : entities_) {
    (void)unused;
    if (entity && entity->material_id == id) {
      ++n;
    }
  }
  for (const SceneNode& node : scene_.nodes()) {
    if (node.material_id == id && entity(node.id) == nullptr) {
      ++n;
    }
  }
  return n;
}

void Document::mark_material_users_dirty(std::uint64_t material_id) {
  if (material_id == 0) {
    return;
  }
  scene_.bump_generation();
  for (const auto& [unused, entity] : entities_) {
    (void)unused;
    if (entity && entity->material_id == material_id) {
      scene_.mark_dirty(entity->id);
    }
  }
  for (const SceneNode& node : scene_.nodes()) {
    if (node.material_id == material_id) {
      scene_.mark_dirty(node.id);
    }
  }
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

void Document::apply_storey_plan(std::vector<Storey>& plan) {
  // 1. 删掉不在表里的楼层（构件留在世界里，改成未归属）。
  std::vector<std::uint64_t> doomed;
  for (const Storey& storey : bim_.storeys()) {
    const bool keep = std::any_of(plan.begin(), plan.end(), [&storey](const Storey& wanted) {
      return wanted.id != 0 && wanted.id == storey.id;
    });
    if (!keep) {
      doomed.push_back(storey.id);
    }
  }
  for (const std::uint64_t id : doomed) {
    remove_storey(id);
  }

  // 2. 新增 / 恢复 / 改参数。
  bool moved = false;
  bool renamed = false;
  for (Storey& wanted : plan) {
    if (wanted.id == 0) {
      wanted.id = add_storey(wanted.name, wanted.elevation).id;
    } else if (bim_.find_storey(wanted.id) == nullptr) {
      insert_storey(wanted);
    }
    Storey* stored = bim_.find_storey(wanted.id);
    if (stored == nullptr) {
      continue;
    }
    const bool elevation_changed = std::abs(stored->elevation - wanted.elevation) > 1e-9;
    renamed = renamed || stored->name != wanted.name;
    stored->name = wanted.name;
    stored->elevation = wanted.elevation;
    stored->height = wanted.height > 0.0 ? wanted.height : kDefaultWallHeight;
    stored->mezzanine = wanted.mezzanine;
    if (SceneNode* node = scene_.find(wanted.id)) {
      node->name = wanted.name;
    }
    if (elevation_changed) {
      moved = true;
      resync_storey_children(wanted.id);
    }
  }

  if (moved || renamed) {
    recompute_scene();
    mark_dirty();
  }
}

void Document::resync_storey_children(std::uint64_t storey_id) {
  const double elevation = bim_.storey_elevation(storey_id);
  for (auto& [unused, entity_ptr] : entities_) {
    (void)unused;
    if (entity_ptr == nullptr || !entity_ptr->location ||
        entity_ptr->location->storey_id() != storey_id) {
      continue;
    }
    entity_ptr->sync_from_location(elevation);
    scene_.set_transform(entity_ptr->id, entity_ptr->local_transform);
  }
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
  // 墙走了，端点的交接跟着走：自己和邻墙都要重新斜接。
  if (is_wall_host(*target)) {
    (void)remesh_wall_neighborhood(*this, entity_id);
  }
  recompute_scene();
  mark_dirty();
  return true;
}

void Document::drop_unref_mesh(std::uint64_t id) {
  if (id == 0 || mesh_referenced(id) || tess_cache_.uses_asset(id) ||
      import_shapes_.count(id) != 0) {
    return;
  }
  remove_mesh(id);
}

void Document::bind_work_lod(std::uint64_t geometry_id) {
  if (geometry_id == 0) {
    return;
  }
  tess_cache_.bind(geometry_id, MeshLod::Work, geometry_id);
}

void Document::ensure_feature_coarse_lod(Entity& entity) {
  if (entity.mesh_asset_id == 0 || entity.is_sketch_entity()) {
    return;
  }
  const std::uint64_t geometry_id = entity.mesh_asset_id;
  bind_work_lod(geometry_id);
  if (tess_cache_.set_for(geometry_id).coarse != 0) {
    return;
  }
  if (const MeshAsset* work = mesh(geometry_id); work != nullptr && work->cpu.line_list) {
    return;
  }
  // 粗档同样按「墙-墙倒角 + 开口切减」造型，否则远处会退回硬拼的墙。
  Result<MeshCpu> coarse =
      geometry_builder().build(wall_render_model(entity, *this), kMeshLodCoarseDeflection);
  if (!coarse) {
    return;
  }
  if (coarse->line_list || coarse->indices.empty()) {
    return;
  }
  MeshAsset& stored = intern_mesh(entity.name, std::move(*coarse));
  tess_cache_.bind(geometry_id, MeshLod::Coarse, stored.id);
}

void Document::invalidate_geometry_lods(std::uint64_t geometry_id) {
  if (geometry_id == 0) {
    return;
  }
  const LodMeshSet set = tess_cache_.set_for(geometry_id);
  tess_cache_.invalidate(geometry_id);
  TessWorker::instance().cancel_geometry(geometry_id);
  if (set.coarse != 0 && set.coarse != geometry_id) {
    drop_unref_mesh(set.coarse);
  }
  if (set.close != 0 && set.close != geometry_id) {
    drop_unref_mesh(set.close);
  }
  if (set.work != 0 && set.work != geometry_id) {
    drop_unref_mesh(set.work);
  }
}

std::function<Result<MeshCpu>()> Document::make_tess_fn(std::uint64_t geometry_id,
                                                        MeshLod lod) const {
  if (const Entity* e = entity_for_mesh(geometry_id); e != nullptr && !e->is_sketch_entity()) {
    // 墙：墙-墙交接倒角 + 宿主开口切减；其它实体就是自己的特征树。
    FeatureModel model = wall_render_model(*e, *this);
    const double deflection = mesh_lod_deflection(lod, false);
    return [model = std::move(model), deflection]() {
      return geometry_builder().build(model, deflection);
    };
  }
  if (const auto it = import_shapes_.find(geometry_id); it != import_shapes_.end() &&
                                                        it->second != nullptr) {
    Shape* shape = it->second.get();
    const double deflection = mesh_lod_deflection(lod, true);
    return [shape, deflection]() { return shape->tessellate(deflection); };
  }
  return {};
}

void Document::enqueue_lod_request(LodRequest request) {
  if (request.geometry_id == 0 || request.lod == MeshLod::Box) {
    return;
  }
  const std::uint64_t existing = tess_cache_.mesh_for(request.geometry_id, request.lod);
  if (existing != 0) {
    if (const MeshAsset* m = mesh(existing); m != nullptr && !m->cpu.vertices.empty()) {
      return;
    }
    if (request.lod == MeshLod::Work && existing == request.geometry_id) {
      if (const MeshAsset* m = mesh(existing); m != nullptr && !m->cpu.vertices.empty()) {
        return;
      }
    }
  }
  if (tess_cache_.pending(request.geometry_id, request.lod)) {
    return;
  }
  auto run = make_tess_fn(request.geometry_id, request.lod);
  if (!run) {
    return;
  }
  tess_cache_.mark_pending(request.geometry_id, request.lod);
  TessWorker::instance().enqueue(request.geometry_id, request.lod, std::move(run));
}

std::vector<std::uint64_t> Document::apply_completed_tess_jobs() {
  std::vector<std::uint64_t> uploaded;
  for (TessJobResult& job : TessWorker::instance().take_completed()) {
    tess_cache_.clear_pending(job.geometry_id, job.lod);
    if (!job.mesh) {
      continue;
    }
    if (!mesh_referenced(job.geometry_id) && import_shapes_.count(job.geometry_id) == 0) {
      continue;
    }
    MeshAsset& stored = intern_mesh("lod", std::move(*job.mesh));
    tess_cache_.bind(job.geometry_id, job.lod, stored.id);
    uploaded.push_back(stored.id);
  }
  return uploaded;
}

void Document::register_mesh_hash(MeshAsset& asset) {
  if (asset.content_hash == 0) {
    asset.content_hash = mesh_content_hash(asset.cpu);
  }
  mesh_by_hash_[asset.content_hash] = asset.id;
}

void Document::unregister_mesh_hash(const MeshAsset& asset) {
  if (asset.content_hash == 0) {
    return;
  }
  const auto it = mesh_by_hash_.find(asset.content_hash);
  if (it != mesh_by_hash_.end() && it->second == asset.id) {
    mesh_by_hash_.erase(it);
  }
}

void Document::remove_mesh(std::uint64_t id) {
  const auto it = meshes_.find(id);
  if (it == meshes_.end()) {
    return;
  }
  unregister_mesh_hash(it->second);
  meshes_.erase(it);
}

bool Document::mesh_referenced(std::uint64_t id) const {
  if (id == 0) {
    return false;
  }
  for (const auto& node : scene_.nodes()) {
    if (node.mesh_asset_id == id) {
      return true;
    }
  }
  return false;
}

MeshAsset& Document::intern_mesh(std::string name, MeshCpu cpu) {
  const std::uint64_t hash = mesh_content_hash(cpu);
  if (const auto it = mesh_by_hash_.find(hash); it != mesh_by_hash_.end()) {
    if (MeshAsset* existing = mesh(it->second)) {
      if (mesh_cpu_equal(existing->cpu, cpu)) {
        return *existing;
      }
    }
  }
  MeshAsset asset{};
  asset.name = std::move(name);
  asset.cpu = std::move(cpu);
  asset.content_hash = hash;
  return add_mesh(std::move(asset));
}

bool Document::replace_entity_mesh(std::uint64_t entity_id, MeshCpu cpu) {
  Entity* target = entity(entity_id);
  if (target == nullptr) {
    return false;
  }
  const std::uint64_t old_id = target->mesh_asset_id;
  MeshAsset& stored = intern_mesh(target->name, std::move(cpu));
  target->mesh_asset_id = stored.id;
  if (SceneNode* node = scene_.find(entity_id)) {
    node->mesh_asset_id = stored.id;
    scene_.bump_generation();
    scene_.mark_dirty(entity_id);
  }
  if (old_id != stored.id) {
    if (!mesh_referenced(old_id)) {
      invalidate_geometry_lods(old_id);
      if (!tess_cache_.uses_asset(old_id)) {
        remove_mesh(old_id);
      }
    }
  }
  ensure_feature_coarse_lod(*target);
  return true;
}

// 只接收已求值的实体 + 几何，不做造型（造型在 Entity::createGeom，见 entity.cpp）。
Entity* Document::add_entity(std::unique_ptr<Entity> entity, MeshCpu mesh) {
  MeshAsset& stored_mesh = intern_mesh(entity->name, std::move(mesh));
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
      case EntityKind::StructuralWall:
      case EntityKind::Foundation:
        preset = "Concrete";
        break;
      case EntityKind::Window:
        preset = "Glass";
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
  if (SceneNode* n = scene_.find(raw->id)) {
    n->material_id = raw->material_id;
  }
  // 新增的墙可能正落在邻墙端点上：两侧自动斜接。
  if (is_wall_host(*raw)) {
    (void)remesh_wall_neighborhood(*this, raw->id);
  }
  ensure_feature_coarse_lod(*raw);
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
  // 交接的邻墙会因为这一面墙消失而少了斜接面，需要重新造型。
  std::vector<std::uint64_t> neighbors = wall_neighborhood(*this, id);
  bim_.remove_involving(id);
  entities_.erase(it);
  scene_.remove_node(id);
  if (!mesh_referenced(mesh_id)) {
    invalidate_geometry_lods(mesh_id);
    if (!tess_cache_.uses_asset(mesh_id)) {
      remove_mesh(mesh_id);
    }
  }
  for (const std::uint64_t neighbor : neighbors) {
    if (neighbor != id && entity(neighbor) != nullptr) {
      (void)remesh_wall(*this, neighbor);
    }
  }
  recompute_scene();
  mark_dirty();
}

void Document::insert_entity(std::unique_ptr<Entity> entity, MeshAsset mesh) {
  const std::uint64_t id = entity->id;
  const std::uint64_t mesh_id = entity->mesh_asset_id;
  if (this->mesh(mesh_id) == nullptr) {
    insert_mesh(std::move(mesh));
  }

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
  if (Entity* raw = entities_[id].get(); raw != nullptr) {
    if (raw->grips.empty()) {
      sync_entity_grips(*raw);
    }
    // 撤销删除 / 重做新建：邻墙要重新认一次交接。
    if (is_wall_host(*raw)) {
      (void)remesh_wall_neighborhood(*this, id);
    }
    ensure_feature_coarse_lod(*raw);
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
  if (render_snapshot_) {
    std::vector<SceneDrawItem> items;
    items.reserve(render_snapshot_->items.size());
    for (SceneDrawItem item : render_snapshot_->items) {
      if (const SceneNode* n = scene_.find(item.node_id)) {
        item.selected = n->selected;
      }
      if (frustum != nullptr && item.bounds.valid() && !frustum->intersects(item.bounds)) {
        continue;
      }
      items.push_back(std::move(item));
    }
    return items;
  }

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
    const Entity* e = entity(node.id);
    std::uint64_t material_id = 0;
    if (e != nullptr) {
      item.category_color = display_color_for_kind(e->kind());
      material_id = e->material_id;
      if (e->is_sketch_entity() && material_id == 0) {
        item.color = display_color_for_kind(e->kind());
        item.lines = true;
      }
    } else {
      material_id = node.material_id;
    }
    if (material_id != 0) {
      if (const Material* m = material(material_id)) {
        item.color = m->base_color;
        item.roughness = m->roughness;
        item.metallic = m->metallic;
        item.opacity = m->opacity;
        item.albedo_texture_id = m->albedo_texture_id;
        item.normal_texture_id = m->normal_texture_id;
        item.orm_texture_id = m->orm_texture_id;
        item.tex = m->tex;
      }
    }
    if (const MeshAsset* asset = mesh(node.mesh_asset_id); asset != nullptr && asset->cpu.line_list) {
      item.lines = true;
    }
    items.push_back(item);
  }
  return items;
}

RenderScene Document::capture_render_scene(RenderScene::View view, const Frustum* frustum) const {
  std::unordered_map<std::uint64_t, MeshCpu> meshes;
  for (const auto& [id, asset] : meshes_) {
    meshes.emplace(id, asset.cpu);
  }
  if (render_snapshot_) {
    for (const auto& [id, mesh] : render_snapshot_->meshes) {
      meshes.emplace(id, mesh);
    }
  }
  std::unordered_map<std::uint64_t, TextureAsset> textures;
  for (const auto& [id, tex] : textures_.assets()) {
    textures.emplace(id, tex);
  }
  if (render_snapshot_) {
    for (const auto& [id, tex] : render_snapshot_->textures) {
      textures.emplace(id, tex);
    }
  }
  RenderScene scene =
      bake_render_scene(render_items(frustum), meshes, std::move(view), name_, textures);

  RenderSceneDebugGraph graph;
  graph.nodes.reserve(scene_.nodes().size());
  for (const SceneNode& node : scene_.nodes()) {
    RenderSceneNodeDebug debug{};
    debug.id = node.id;
    debug.name = node.name;
    debug.parent = node.parent;
    debug.children = node.children;
    debug.mesh_asset_id = node.mesh_asset_id;
    debug.local_transform = node.local_transform;
    debug.world_transform = node.world_transform;
    debug.local_bounds = node.local_bounds;
    debug.world_bounds = node.world_bounds;
    debug.selected = node.selected;
    graph.nodes.push_back(std::move(debug));
  }
  graph.lod_sets = tess_cache_.snapshot();
  for (const SceneNode& node : scene_.nodes()) {
    if (node.mesh_asset_id == 0) {
      continue;
    }
    bool lines = false;
    if (const MeshAsset* asset = mesh(node.mesh_asset_id); asset != nullptr) {
      lines = asset->cpu.line_list;
    }
    const float projected_px = projected_aabb_pixels(
        node.world_bounds, scene.view.eye_position, scene.view.fovy,
        static_cast<float>(std::max<std::uint32_t>(scene.view.height, 1u)));
    graph.lod_by_node[node.id] =
        select_mesh_lod(projected_px, std::nullopt, node.selected, lines);
  }
  scene.debug_graph = std::move(graph);
  return scene;
}

void Document::set_render_snapshot(RenderScene scene) { render_snapshot_ = std::move(scene); }

Document document_from_render_scene(RenderScene scene) {
  Document doc(scene.source.empty() ? "Render snapshot" : scene.source);
  const RenderSceneDebugGraph graph = scene.debug_graph;
  for (auto& [id, mesh] : scene.meshes) {
    MeshAsset asset{};
    asset.id = id;
    asset.name = "mesh-" + std::to_string(id);
    asset.cpu = mesh;
    doc.insert_mesh(std::move(asset));
  }
  if (!graph.nodes.empty()) {
    for (const RenderSceneNodeDebug& debug : graph.nodes) {
      SceneNode node{};
      node.id = debug.id;
      node.name = debug.name.empty() ? "node-" + std::to_string(debug.id) : debug.name;
      node.parent = debug.parent;
      node.children = debug.children;
      node.mesh_asset_id = debug.mesh_asset_id;
      node.local_transform = debug.local_transform;
      node.world_transform = debug.world_transform;
      node.local_bounds = debug.local_bounds;
      node.world_bounds = debug.world_bounds;
      node.selected = debug.selected;
      doc.scene().insert_node(std::move(node));
    }
  } else {
    for (const auto& item : scene.items) {
      SceneNode node{};
      node.id = item.node_id;
      node.name = "node-" + std::to_string(item.node_id);
      node.mesh_asset_id = item.mesh_asset_id;
      node.local_transform = item.transform;
      node.color = item.color;
      node.selected = item.selected;
      doc.scene().insert_node(std::move(node));
    }
  }
  doc.replace_textures(scene.textures);
  doc.recompute_scene();
  doc.set_render_snapshot(std::move(scene));
  return doc;
}

std::uint64_t Document::add_import_mesh(std::string name, MeshCpu mesh, Mat4 transform, Vec3 color,
                                        std::uint64_t material_id) {
  MeshAsset& stored_mesh = intern_mesh(std::move(name), std::move(mesh));

  SceneNode node{};
  node.name = stored_mesh.name;
  node.mesh_asset_id = stored_mesh.id;
  node.material_id = material_id;
  node.local_transform = transform;
  node.color = color;
  scene_.add_node(std::move(node));
  bind_work_lod(stored_mesh.id);
  recompute_scene();
  mark_dirty();
  return stored_mesh.id;
}

std::uint64_t Document::add_import_shape(std::string name, std::unique_ptr<Shape> shape,
                                        Mat4 transform, Vec3 color) {
  if (!shape) {
    return 0;
  }
  MeshAsset shell{};
  shell.name = name;
  shell.cpu.bounds = shape->bounds();
  MeshAsset& stored = add_mesh(std::move(shell));
  import_shapes_[stored.id] = std::move(shape);

  SceneNode node{};
  node.name = std::move(name);
  node.mesh_asset_id = stored.id;
  node.local_transform = transform;
  node.color = color;
  scene_.add_node(std::move(node));
  recompute_scene();
  mark_dirty();
  return stored.id;
}

Shape* Document::import_shape(std::uint64_t geometry_id) {
  const auto it = import_shapes_.find(geometry_id);
  return it == import_shapes_.end() ? nullptr : it->second.get();
}

const MeshAsset* Document::resolved_mesh(std::uint64_t geometry_id) const {
  if (geometry_id == 0) {
    return nullptr;
  }
  const LodMeshSet set = tess_cache_.set_for(geometry_id);
  const std::uint64_t candidates[] = {set.close, set.work, set.coarse, geometry_id};
  for (const std::uint64_t id : candidates) {
    if (id == 0) {
      continue;
    }
    if (const MeshAsset* asset = mesh(id); asset != nullptr && !asset->cpu.vertices.empty()) {
      return asset;
    }
  }
  return mesh(geometry_id);
}

}  // namespace tamias
