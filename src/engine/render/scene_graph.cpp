#include "engine/render/scene_graph.h"

#include "engine/core/log.h"
#include "engine/render/batch_key.h"
#include "engine/render/gpu_instance.h"
#include "engine/render/mesh_lod.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>

namespace tamias {

// ---------------------------------------------------------------------------
// 节点 accept：统一转发到 visitor 的 apply
// ---------------------------------------------------------------------------

void GroupNode::accept(RenderVisitor& visitor) { visitor.apply(*this); }
void TransformNode::accept(RenderVisitor& visitor) { visitor.apply(*this); }
void StateGroupNode::accept(RenderVisitor& visitor) { visitor.apply(*this); }
void DrawableNode::accept(RenderVisitor& visitor) { visitor.apply(*this); }

void GroupNode::add_child(std::unique_ptr<RenderNode> child) {
  children.push_back(std::move(child));
}
void GroupNode::remove_child(const RenderNode* child) {
  std::erase_if(children, [child](const auto& c) { return c.get() == child; });
}
void TransformNode::add_child(std::unique_ptr<RenderNode> child) {
  children.push_back(std::move(child));
}
void StateGroupNode::add_child(std::unique_ptr<RenderNode> child) {
  children.push_back(std::move(child));
}

// ---------------------------------------------------------------------------
// 基类访问者默认行为：只做孩子遍历，不做状态/矩阵处理
// ---------------------------------------------------------------------------

void RenderVisitor::apply(GroupNode& node) { traverse(node, *this); }
void RenderVisitor::apply(TransformNode& node) { traverse(node, *this); }
void RenderVisitor::apply(StateGroupNode& node) { traverse(node, *this); }
void RenderVisitor::apply(DrawableNode&) {}

void RenderVisitor::traverse(GroupNode& node, RenderVisitor& visitor) {
  for (auto& child : node.children) {
    child->accept(visitor);
  }
}
void RenderVisitor::traverse(TransformNode& node, RenderVisitor& visitor) {
  for (auto& child : node.children) {
    child->accept(visitor);
  }
}
void RenderVisitor::traverse(StateGroupNode& node, RenderVisitor& visitor) {
  for (auto& child : node.children) {
    child->accept(visitor);
  }
}
void RenderVisitor::traverse(DrawableNode&, RenderVisitor&) {}

// ---------------------------------------------------------------------------
// StateCommands：把数据写进上下文状态（显式覆盖，不隐式继承）
// ---------------------------------------------------------------------------

void BindMaterialCommand::record(SceneGraphDrawContext& ctx) {
  ctx.material_color = color;
  ctx.category_color = category_color;
  ctx.material_roughness = roughness;
  ctx.material_metallic = metallic;
  ctx.material_opacity = opacity;
  ctx.material_albedo_texture_id = albedo_texture_id;
  ctx.material_normal_texture_id = normal_texture_id;
  ctx.material_orm_texture_id = orm_texture_id;
  ctx.material_tex = tex;
}

std::unique_ptr<StateCommand> BindMaterialCommand::clone() const {
  return std::make_unique<BindMaterialCommand>(*this);
}

void SetSelectedCommand::record(SceneGraphDrawContext& ctx) { ctx.selected = selected; }

std::unique_ptr<StateCommand> SetSelectedCommand::clone() const {
  return std::make_unique<SetSelectedCommand>(*this);
}

void SetLinesCommand::record(SceneGraphDrawContext& ctx) { ctx.lines = lines; }

std::unique_ptr<StateCommand> SetLinesCommand::clone() const {
  return std::make_unique<SetLinesCommand>(*this);
}

// ---------------------------------------------------------------------------
// RecordCommands：录制整棵树
// ---------------------------------------------------------------------------

void RecordCommands::apply(GroupNode& node) {
  if (!node.visible) {
    return;
  }
  RenderVisitor::traverse(node, *this);
}

void RecordCommands::apply(TransformNode& node) {
  if (!node.visible) {
    return;
  }
  matrix_stack_.push_back(matrix_stack_.back() * node.matrix);
  RenderVisitor::traverse(node, *this);
  matrix_stack_.pop_back();
}

void RecordCommands::apply(StateGroupNode& node) {
  if (!node.visible) {
    return;
  }
  for (const auto& command : node.commands) {
    command->record(ctx_);
  }
  RenderVisitor::traverse(node, *this);
}

void RecordCommands::apply(DrawableNode& node) {
  if (!node.visible || ctx_.command_list == nullptr || ctx_.view_proj == nullptr) {
    return;
  }
  if (ctx_.frustum != nullptr && !ctx_.frustum->intersects(node.bounds)) {
    return;  // 视锥剔除（无效包围盒永不剔除，与 render_items 语义一致）
  }
  if (ctx_.hidden_nodes != nullptr && ctx_.hidden_nodes->count(node.node_id) != 0) {
    return;  // 按语义节点 id 隐藏（视口 floor/类别/isolate 过滤）
  }

  const float projected_px = projected_aabb_pixels(node.bounds, ctx_.eye_position, ctx_.fovy,
                                                   ctx_.framebuffer_height);
  const bool lod_enabled = ctx_.lod_sets != nullptr || ctx_.lod_box_mesh != nullptr;
  std::optional<MeshLod> previous;
  MeshLod lod = MeshLod::Work;
  if (lod_enabled) {
    if (ctx_.lod_hysteresis != nullptr) {
      const auto it = ctx_.lod_hysteresis->find(node.node_id);
      if (it != ctx_.lod_hysteresis->end()) {
        previous = it->second;
      }
    }
    lod = select_mesh_lod(projected_px, previous, ctx_.selected, ctx_.lines);
    if (ctx_.lod_hysteresis != nullptr) {
      (*ctx_.lod_hysteresis)[node.node_id] = lod;
    }
    if (mesh_lod_skip_draw(projected_px, ctx_.selected)) {
      return;
    }
  }

  Mat4 world = matrix_stack_.back();

  // 解析网格资产：asset id -> gpu mesh id -> GpuMesh。LOD 在录制期选档，不改场景图。
  if (ctx_.asset_to_gpu == nullptr || ctx_.meshes == nullptr) {
    return;
  }

  const auto lod_asset = [&](MeshLod level) -> std::uint64_t {
    if (ctx_.lod_sets != nullptr) {
      const auto it = ctx_.lod_sets->find(node.mesh_asset_id);
      if (it != ctx_.lod_sets->end()) {
        const std::uint64_t id = it->second.asset(level);
        if (id != 0) {
          return id;
        }
      }
    }
    return level == MeshLod::Work ? node.mesh_asset_id : 0;
  };
  const auto gpu_for_asset = [&](std::uint64_t asset_id)
      -> std::pair<const GpuMesh*, std::uint64_t> {
    if (asset_id == 0) {
      return {nullptr, 0};
    }
    const auto gpu_it = ctx_.asset_to_gpu->find(asset_id);
    if (gpu_it == ctx_.asset_to_gpu->end()) {
      return {nullptr, 0};
    }
    const auto mesh_it = ctx_.meshes->find(gpu_it->second);
    if (mesh_it == ctx_.meshes->end()) {
      return {nullptr, 0};
    }
    return {&mesh_it->second, gpu_it->second};
  };
  const auto request_lod = [&](MeshLod level) {
    if (ctx_.lod_requests == nullptr || level == MeshLod::Box) {
      return;
    }
    ctx_.lod_requests->push_back(LodRequest{node.mesh_asset_id, level});
  };

  const GpuMesh* mesh = nullptr;
  std::uint64_t gpu_mesh_id = 0;
  MeshLod draw_lod = lod;
  if (draw_lod == MeshLod::Box && ctx_.lod_box_mesh == nullptr) {
    draw_lod = MeshLod::Work;  // 无 L0 盒时保持旧路径：直接画工作网
  }
  if (draw_lod == MeshLod::Box) {
    world = lod_box_world_matrix(node.bounds);
    mesh = ctx_.lod_box_mesh;
    gpu_mesh_id = ctx_.lod_box_gpu_id;
  } else {
    auto found = gpu_for_asset(lod_asset(draw_lod));
    if (found.first == nullptr && draw_lod == MeshLod::Work) {
      request_lod(MeshLod::Work);
      found = gpu_for_asset(lod_asset(MeshLod::Coarse));
    } else if (found.first == nullptr && draw_lod == MeshLod::Coarse) {
      request_lod(MeshLod::Coarse);
      found = gpu_for_asset(lod_asset(MeshLod::Work));
    }
    if (found.first != nullptr) {
      mesh = found.first;
      gpu_mesh_id = found.second;
      if (gpu_for_asset(lod_asset(draw_lod)).first == nullptr) {
        request_lod(draw_lod);
      }
    } else if (ctx_.lod_box_mesh != nullptr) {
      request_lod(draw_lod);
      world = lod_box_world_matrix(node.bounds);
      mesh = ctx_.lod_box_mesh;
      gpu_mesh_id = ctx_.lod_box_gpu_id;
    }
  }
  if (mesh == nullptr) {
    return;
  }

  const bool as_lines = ctx_.lines || mesh->line_list;
  const bool use_material = !as_lines && ctx_.mode_value > 1.5f;
  const bool transmissive = use_material && ctx_.material_opacity < 0.999f;
  if (ctx_.transparent_pass) {
    if (!transmissive) {
      return;
    }
  } else if (transmissive) {
    return;
  }

  Texture* bound_albedo = ctx_.default_texture;
  Texture* bound_normal = ctx_.default_normal != nullptr ? ctx_.default_normal : ctx_.default_texture;
  Texture* bound_orm = ctx_.default_orm != nullptr ? ctx_.default_orm : ctx_.default_texture;
  bool has_albedo = false;
  bool has_normal = false;
  bool has_orm = false;
  if (use_material && ctx_.texture_asset_to_gpu != nullptr && ctx_.textures != nullptr) {
    if (ctx_.material_albedo_texture_id != 0) {
      const auto tex_it = ctx_.texture_asset_to_gpu->find(ctx_.material_albedo_texture_id);
      if (tex_it != ctx_.texture_asset_to_gpu->end()) {
        const auto gtex_it = ctx_.textures->find(tex_it->second);
        if (gtex_it != ctx_.textures->end()) {
          bound_albedo = gtex_it->second.texture.get();
          has_albedo = true;
        }
      }
    }
    if (ctx_.material_normal_texture_id != 0) {
      const auto tex_it = ctx_.texture_asset_to_gpu->find(ctx_.material_normal_texture_id);
      if (tex_it != ctx_.texture_asset_to_gpu->end()) {
        const auto gtex_it = ctx_.textures->find(tex_it->second);
        if (gtex_it != ctx_.textures->end()) {
          bound_normal = gtex_it->second.texture.get();
          has_normal = true;
        }
      }
    }
    if (ctx_.material_orm_texture_id != 0) {
      const auto tex_it = ctx_.texture_asset_to_gpu->find(ctx_.material_orm_texture_id);
      if (tex_it != ctx_.texture_asset_to_gpu->end()) {
        const auto gtex_it = ctx_.textures->find(tex_it->second);
        if (gtex_it != ctx_.textures->end()) {
          bound_orm = gtex_it->second.texture.get();
          has_orm = true;
        }
      }
    }
  }
  if (ctx_.texture_diag_logged != nullptr && !*ctx_.texture_diag_logged) {
    log_info("draw texture diag: albedo_id=" +
             std::to_string(ctx_.material_albedo_texture_id) +
             " has_albedo=" + (has_albedo ? "1" : "0") +
             " gpu_tex_count=" +
             std::to_string(ctx_.textures != nullptr ? ctx_.textures->size() : 0u) +
             " color=" + std::to_string(ctx_.material_color.x) + "," +
             std::to_string(ctx_.material_color.y) + "," +
             std::to_string(ctx_.material_color.z));
    *ctx_.texture_diag_logged = true;
  }

  PipelineState* pipeline = nullptr;
  if (transmissive) {
    pipeline = ctx_.blend_pipeline;
  } else if (as_lines) {
    pipeline = ctx_.entity_line_pipeline;
  } else if (ctx_.mode_value == 0.f) {
    pipeline = ctx_.wire_pipeline;
  } else {
    pipeline = ctx_.shaded_pipeline;
  }
  if (pipeline == nullptr) {
    return;
  }

  const Vec3 color = use_material ? ctx_.material_color : ctx_.category_color;
  GpuInstance instance =
      make_gpu_instance(world, color, use_material ? ctx_.material_opacity : 1.f,
                        use_material ? ctx_.material_roughness : 0.6f,
                        use_material ? ctx_.material_metallic : 0.f, ctx_.selected);
  if (use_material) {
    apply_texture_transform(instance, ctx_.material_tex);
  }

  PendingBatch batch{};
  batch.key.gpu_mesh_id = gpu_mesh_id;
  batch.key.pipeline = pipeline;
  batch.key.albedo = bound_albedo;
  batch.key.normal = bound_normal;
  batch.key.orm = bound_orm;
  batch.key.lines = as_lines;
  batch.key.transparent = transmissive;
  batch.pipeline = pipeline;
  batch.albedo = bound_albedo;
  batch.normal = bound_normal;
  batch.orm = bound_orm;
  batch.mesh = mesh;
  batch.has_albedo = has_albedo;
  batch.has_normal = has_normal;
  batch.has_orm = has_orm;
  batch.as_lines = as_lines;

  if (transmissive) {
    batch.instances.push_back(instance);
    flush_batch(batch);
    return;
  }
  enqueue(std::move(batch), instance);
}

void RecordCommands::enqueue(PendingBatch batch, GpuInstance instance) {
  const auto it = batch_index_.find(batch.key);
  if (it != batch_index_.end()) {
    batches_[it->second].instances.push_back(instance);
    return;
  }
  batch.instances.push_back(instance);
  batch_index_.emplace(batch.key, batches_.size());
  batches_.push_back(std::move(batch));
}

void RecordCommands::flush_batch(PendingBatch& batch) {
  if (batch.instances.empty() || ctx_.command_list == nullptr || ctx_.view_proj == nullptr ||
      batch.pipeline == nullptr || batch.mesh == nullptr || batch.mesh->vertex_buffer == nullptr ||
      batch.mesh->index_buffer == nullptr) {
    return;
  }

  ctx_.command_list->set_pipeline(*batch.pipeline);
  if (batch.albedo != nullptr) {
    ctx_.command_list->set_texture(*batch.albedo, kTextureSlotAlbedo);
  }
  if (batch.normal != nullptr) {
    ctx_.command_list->set_texture(*batch.normal, kTextureSlotNormal);
  }
  if (batch.orm != nullptr) {
    ctx_.command_list->set_texture(*batch.orm, kTextureSlotOrm);
  }

  if (ctx_.recorded_instances != nullptr) {
    ctx_.recorded_instances->insert(ctx_.recorded_instances->end(), batch.instances.begin(),
                                    batch.instances.end());
  }

  const auto bytes = static_cast<std::uint64_t>(batch.instances.size() * sizeof(GpuInstance));
  if (ctx_.grow_instance_buffer) {
    const std::uint64_t needed =
        (ctx_.instance_write_offset != nullptr ? *ctx_.instance_write_offset : 0) + bytes;
    ctx_.instance_buffer = ctx_.grow_instance_buffer(needed);
  }
  std::uint64_t offset = 0;
  if (ctx_.instance_write_offset != nullptr) {
    offset = *ctx_.instance_write_offset;
  }
  if (ctx_.instance_buffer != nullptr) {
    ctx_.instance_buffer->write(
        offset, std::as_bytes(std::span<const GpuInstance>{batch.instances.data(),
                                                           batch.instances.size()}));
    ctx_.command_list->set_instance_buffer(*ctx_.instance_buffer, offset);
  }
  if (ctx_.instance_write_offset != nullptr) {
    *ctx_.instance_write_offset += bytes;
  }

  PushConstants pc{};
  pc.mvp = *ctx_.view_proj;
  pc.model = Mat4::identity();
  pc.color[0] = pc.color[1] = pc.color[2] = 1.f;
  pc.color[3] = 1.f;
  pc.material[0] = 0.6f;
  pc.material[1] = 0.f;
  pc.material[2] = batch.has_albedo ? 1.f : 0.f;
  pc.material[3] = batch.has_normal ? 1.f : 0.f;
  pc.light_dir_selected[0] = 0.45f;
  pc.light_dir_selected[1] = 0.35f;
  pc.light_dir_selected[2] = 0.82f;
  pc.light_dir_selected[3] = batch.has_orm ? 1.f : 0.f;
  pc.eye_pos_mode[0] = ctx_.eye_position.x;
  pc.eye_pos_mode[1] = ctx_.eye_position.y;
  pc.eye_pos_mode[2] = ctx_.eye_position.z;
  pc.eye_pos_mode[3] = batch.as_lines ? 3.f : ctx_.mode_value;
  pc.lighting[0] = ctx_.exposure;
  pc.lighting[1] = ctx_.key_light_intensity;
  pc.lighting[2] = ctx_.ibl_max_mip;
  pc.lighting[3] = batch.mesh->has_texcoord ? 1.f : 0.f;
  ctx_.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
  ctx_.command_list->set_vertex_buffer(*batch.mesh->vertex_buffer);
  ctx_.command_list->set_index_buffer(*batch.mesh->index_buffer);
  DrawIndexedDesc draw{};
  draw.index_count = batch.mesh->index_count;
  draw.instance_count = static_cast<std::uint32_t>(batch.instances.size());
  ctx_.command_list->draw_indexed(draw);
  if (ctx_.stats != nullptr) {
    ++ctx_.stats->draws;
    if (!batch.as_lines) {
      ctx_.stats->triangles += (batch.mesh->index_count / 3) * draw.instance_count;
    }
  }
}

void RecordCommands::flush_all() {
  for (auto& batch : batches_) {
    flush_batch(batch);
  }
  batches_.clear();
  batch_index_.clear();
}

// ---------------------------------------------------------------------------
// 由展平结果构建场景图（当前阶段每帧全量重建）
// ---------------------------------------------------------------------------

namespace {

void bind_item_material(BindMaterialCommand& material, const SceneDrawItem& item) {
  material.color = item.color;
  material.category_color = item.category_color;
  material.roughness = item.roughness;
  material.metallic = item.metallic;
  material.opacity = item.opacity;
  material.albedo_texture_id = item.albedo_texture_id;
  material.normal_texture_id = item.normal_texture_id;
  material.orm_texture_id = item.orm_texture_id;
  material.tex = item.tex;
}

// 单个 item 的子树：Transform(world) → StateGroup(材质/选中/线条) → Drawable。
std::unique_ptr<TransformNode> make_item_subtree(const SceneDrawItem& item) {
  auto transform = std::make_unique<TransformNode>();
  transform->name = "node_" + std::to_string(item.node_id);
  transform->matrix = item.transform;

  auto state = std::make_unique<StateGroupNode>();
  state->name = "state_" + std::to_string(item.node_id);

  auto material = std::make_unique<BindMaterialCommand>();
  bind_item_material(*material, item);
  state->commands.push_back(std::move(material));

  auto selected = std::make_unique<SetSelectedCommand>();
  selected->selected = item.selected;
  state->commands.push_back(std::move(selected));

  auto lines = std::make_unique<SetLinesCommand>();
  lines->lines = item.lines;
  state->commands.push_back(std::move(lines));

  auto drawable = std::make_unique<DrawableNode>();
  drawable->name = "draw_" + std::to_string(item.node_id);
  drawable->node_id = item.node_id;
  drawable->mesh_asset_id = item.mesh_asset_id;
  drawable->bounds = item.bounds;
  state->add_child(std::move(drawable));
  transform->add_child(std::move(state));
  return transform;
}

}  // namespace

std::unique_ptr<RenderNode> build_scene_graph(
    const std::vector<SceneDrawItem>& items,
    std::unordered_map<std::uint64_t, TransformNode*>* node_index) {
  auto root = std::make_unique<GroupNode>();
  root->name = "root";
  if (node_index != nullptr) {
    node_index->clear();
  }
  for (const auto& item : items) {
    auto transform = make_item_subtree(item);
    if (node_index != nullptr) {
      (*node_index)[item.node_id] = transform.get();
    }
    root->add_child(std::move(transform));
  }
  return root;
}

void update_scene_graph(GroupNode& root,
                        std::unordered_map<std::uint64_t, TransformNode*>& node_index,
                        const std::vector<SceneDrawItem>& items,
                        const std::vector<std::uint64_t>& dirty_ids) {
  // id → item 查找表（只按脏 id 查询；每次变更时重建，O(n) 可接受）。
  std::unordered_map<std::uint64_t, const SceneDrawItem*> item_by_id;
  item_by_id.reserve(items.size());
  for (const auto& item : items) {
    item_by_id.emplace(item.node_id, &item);
  }

  for (const std::uint64_t dirty_id : dirty_ids) {
    const auto item_it = item_by_id.find(dirty_id);
    const auto node_it = node_index.find(dirty_id);
    if (item_it == item_by_id.end()) {
      // 已删除：移除子树并注销索引。
      if (node_it != node_index.end()) {
        root.remove_child(node_it->second);
        node_index.erase(node_it);
      }
      continue;
    }

    const SceneDrawItem& item = *item_it->second;
    if (node_it != node_index.end()) {
      // 已存在：就地更新 world matrix / StateGroup 命令 / Drawable 字段。
      TransformNode* transform = node_it->second;
      transform->matrix = item.transform;
      auto* state = dynamic_cast<StateGroupNode*>(transform->children.front().get());
      if (state == nullptr) {
        continue;
      }
      state->commands.clear();
      auto material = std::make_unique<BindMaterialCommand>();
      bind_item_material(*material, item);
      state->commands.push_back(std::move(material));
      auto selected = std::make_unique<SetSelectedCommand>();
      selected->selected = item.selected;
      state->commands.push_back(std::move(selected));
      auto lines = std::make_unique<SetLinesCommand>();
      lines->lines = item.lines;
      state->commands.push_back(std::move(lines));
      if (auto* drawable = dynamic_cast<DrawableNode*>(state->children.front().get())) {
        drawable->node_id = item.node_id;
        drawable->mesh_asset_id = item.mesh_asset_id;
        drawable->bounds = item.bounds;
      }
    } else {
      // 新增：追加子树并注册索引。
      auto transform = make_item_subtree(item);
      TransformNode* raw = transform.get();
      root.add_child(std::move(transform));
      node_index[dirty_id] = raw;
    }
  }
}

}  // namespace tamias
