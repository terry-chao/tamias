#include "engine/render/scene_graph.h"

#include "engine/core/log.h"

#include <cstddef>
#include <span>

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

  const Mat4 world = matrix_stack_.back();

  // 解析网格资产：asset id -> gpu mesh id -> GpuMesh。
  if (ctx_.asset_to_gpu == nullptr || ctx_.meshes == nullptr) {
    return;
  }
  const auto gpu_it = ctx_.asset_to_gpu->find(node.mesh_asset_id);
  if (gpu_it == ctx_.asset_to_gpu->end()) {
    return;
  }
  const auto mesh_it = ctx_.meshes->find(gpu_it->second);
  if (mesh_it == ctx_.meshes->end()) {
    return;
  }
  const GpuMesh& mesh = mesh_it->second;

  const bool as_lines = ctx_.lines || mesh.line_list;
  const bool use_material = !as_lines && ctx_.mode_value > 1.5f;
  const bool transmissive = use_material && ctx_.material_opacity < 0.999f;
  if (ctx_.transparent_pass) {
    if (!transmissive) {
      return;
    }
  } else if (transmissive) {
    return;
  }

  // 绑定 albedo / normal：真实感且已上传 → 实纹理；着色模式只用构件色。
  Texture* bound_albedo = ctx_.default_texture;
  Texture* bound_normal = ctx_.default_normal != nullptr ? ctx_.default_normal : ctx_.default_texture;
  bool has_albedo = false;
  bool has_normal = false;
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

  // Vulkan 的 set_texture 依赖当前 pipeline layout；先绑管线再绑描述符。
  if (transmissive) {
    if (ctx_.blend_pipeline == nullptr) {
      return;
    }
    ctx_.command_list->set_pipeline(*ctx_.blend_pipeline);
  } else if (as_lines) {
    if (ctx_.entity_line_pipeline == nullptr) {
      return;
    }
    ctx_.command_list->set_pipeline(*ctx_.entity_line_pipeline);
  } else {
    if (ctx_.mode_value == 0.f) {
      if (ctx_.wire_pipeline == nullptr) {
        return;
      }
      ctx_.command_list->set_pipeline(*ctx_.wire_pipeline);
    } else {
      if (ctx_.shaded_pipeline == nullptr) {
        return;
      }
      ctx_.command_list->set_pipeline(*ctx_.shaded_pipeline);
    }
  }
  if (bound_albedo != nullptr) {
    ctx_.command_list->set_texture(*bound_albedo, kTextureSlotAlbedo);
  }
  if (bound_normal != nullptr) {
    ctx_.command_list->set_texture(*bound_normal, kTextureSlotNormal);
  }

  const Vec3 color = use_material ? ctx_.material_color : ctx_.category_color;
  PushConstants pc{};
  pc.mvp = *ctx_.view_proj * world;
  pc.model = world;
  pc.color[0] = color.x;
  pc.color[1] = color.y;
  pc.color[2] = color.z;
  pc.color[3] = use_material ? ctx_.material_opacity : 1.f;
  pc.material[0] = use_material ? ctx_.material_roughness : 0.6f;
  pc.material[1] = use_material ? ctx_.material_metallic : 0.f;
  pc.material[2] = has_albedo ? 1.f : 0.f;
  pc.material[3] = has_normal ? 1.f : 0.f;
  pc.light_dir_selected[0] = 0.45f;
  pc.light_dir_selected[1] = 0.35f;
  pc.light_dir_selected[2] = 0.82f;
  pc.light_dir_selected[3] = ctx_.selected ? 1.f : 0.f;
  pc.eye_pos_mode[0] = ctx_.eye_position.x;
  pc.eye_pos_mode[1] = ctx_.eye_position.y;
  pc.eye_pos_mode[2] = ctx_.eye_position.z;
  pc.eye_pos_mode[3] = as_lines ? 3.f : ctx_.mode_value;
  pc.lighting[0] = ctx_.exposure;
  pc.lighting[1] = ctx_.key_light_intensity;
  pc.lighting[2] = ctx_.ibl_max_mip;
  pc.lighting[3] = mesh.has_texcoord ? 1.f : 0.f;
  ctx_.command_list->set_push_constants(std::as_bytes(std::span{&pc, 1}));
  ctx_.command_list->set_vertex_buffer(*mesh.vertex_buffer);
  ctx_.command_list->set_index_buffer(*mesh.index_buffer);
  DrawIndexedDesc draw{};
  draw.index_count = mesh.index_count;
  ctx_.command_list->draw_indexed(draw);
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
