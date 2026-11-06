#include "command/nodes/bake_graph_command.h"

#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/nodes/node_eval.h"
#include "engine/modeling/nodes/node_type.h"

#include <algorithm>
#include <utility>

namespace tamias {

namespace {

constexpr double kBakeDeflection = 0.05;  // 与 rebuild_entity_mesh 的默认离散偏差一致

// 烘焙颜色：中性灰（和普通着色构件一个观感，区别于橙色预览）。
constexpr Vec3 kBakeColor{0.72f, 0.74f, 0.78f};

// 这个节点的输出该不该烘：预览开着，或它就是 output 节点。
bool should_bake(const Graph::Node& node) {
  if (node.type != nullptr && node.type->id == "output.preview") {
    return true;
  }
  return node.preview;
}

}  // namespace

BakeGraphCommand::BakeGraphCommand(Document& document, std::vector<std::uint64_t> node_ids)
    : document_(&document), node_ids_(std::move(node_ids)) {}

Result<void> BakeGraphCommand::execute() {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return Err("BakeGraphCommand: document has no node graph");
  }
  auto kernel = default_kernel();
  if (!kernel) {
    return Err(kernel.error());
  }

  // 烘焙要完整的结果：同步把脏节点算完。
  SolveContext context;
  context.kernel = *kernel;  // default_kernel() 返回 Result<ModelKernel*>
  if (!evaluate_graph(*graph, context)) {
    return Err("BakeGraphCommand: evaluation cancelled");
  }

  baked_.clear();
  baked_node_ids_.clear();
  int index = 0;
  for (const auto& node : graph->nodes()) {
    if (!node_ids_.empty() &&
        std::find(node_ids_.begin(), node_ids_.end(), node->id) == node_ids_.end()) {
      continue;
    }
    if (!should_bake(*node)) {
      continue;
    }
    if (node->outputs.empty()) {
      continue;
    }
    const BodyRef body = node->outputs[0].as_body();
    if (body == nullptr) {
      continue;  // 出错的节点跳过（错误信息留在节点卡片上）
    }
    auto mesh = (**kernel).tessellate(*body, kBakeDeflection);
    if (!mesh) {
      return Err("BakeGraphCommand: tessellate failed: " + mesh.error());
    }
    const std::string name = "Node bake " + std::to_string(++index);
    // 注意：add_import_mesh 返回的是**网格资产 id**（内容寻址，可能复用旧资产），
    // 不是场景节点 id。以前把返回值当节点 id 用，一旦预细分过网格（mesh 计数跑在
    // 节点计数前面）就会 find 到错节点或直接失败，烘焙出来的东西既没进撤销栈、
    // 也选不中删不掉。新节点永远是刚 push 进 scene 的最后一个。
    document_->add_import_mesh(name, *mesh, Mat4::identity(), kBakeColor);
    const SceneNode& stored = document_->scene().nodes().back();
    const MeshAsset* stored_mesh = document_->mesh(stored.mesh_asset_id);
    if (stored_mesh == nullptr) {
      return Err("BakeGraphCommand: scene node missing after add");
    }
    Baked baked;
    baked.node = stored;
    baked.mesh_id = stored_mesh->id;
    baked.asset = *stored_mesh;  // 内容寻址可能复用旧资产，快照拍实际那份
    baked_node_ids_.push_back(stored.id);
    baked_.push_back(std::move(baked));
  }

  if (baked_.empty()) {
    return Err("BakeGraphCommand: no evaluated outputs to bake");
  }
  document_->mark_dirty();
  executed_ = true;
  return {};
}

void BakeGraphCommand::undo() {
  if (!executed_) {
    return;
  }
  for (const Baked& baked : baked_) {
    document_->scene().remove_node(baked.node.id);
    // 网格内容寻址共享：还有别的节点在用（或 LOD 缓存挂着）就不删。
    bool referenced = false;
    for (const SceneNode& node : document_->scene().nodes()) {
      if (node.mesh_asset_id == baked.mesh_id) {
        referenced = true;
        break;
      }
    }
    if (!referenced && !document_->tess_cache().uses_asset(baked.mesh_id)) {
      document_->invalidate_geometry_lods(baked.mesh_id);
      document_->remove_mesh(baked.mesh_id);
    }
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

void BakeGraphCommand::redo() {
  if (!executed_) {
    return;
  }
  // 网格 / 场景节点都按原 id 放回（撤销期间谁引用了它们都不断线）。
  for (const Baked& baked : baked_) {
    if (baked.mesh_id != 0 && document_->mesh(baked.mesh_id) == nullptr) {
      document_->insert_mesh(baked.asset);
    }
    document_->scene().insert_node(baked.node);
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
