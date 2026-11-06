#include "command/delete/delete_scene_node_command.h"

namespace tamias {

DeleteSceneNodeCommand::DeleteSceneNodeCommand(Document& document, std::uint64_t node_id)
    : document_(&document), node_id_(node_id) {}

Result<void> DeleteSceneNodeCommand::execute() {
  if (document_ == nullptr) {
    return Err("DeleteSceneNodeCommand: no document");
  }
  if (node_id_ == 0) {
    return Err("DeleteSceneNodeCommand: invalid node id");
  }
  const SceneNode* node = document_->scene().find(node_id_);
  if (node == nullptr) {
    return Err("DeleteSceneNodeCommand: node not found");
  }
  if (document_->entity(node_id_) != nullptr) {
    // 有实体的构件走 delete_entity：那边还要处理 BIM 关系 / 邻墙交接。
    return Err("DeleteSceneNodeCommand: node has an entity; use delete_entity");
  }
  node_ = *node;
  if (const MeshAsset* mesh = document_->mesh(node_.mesh_asset_id); mesh != nullptr) {
    mesh_ = *mesh;
    has_mesh_ = true;
  }
  remove_now();
  executed_ = true;
  return {};
}

void DeleteSceneNodeCommand::remove_now() {
  document_->scene().remove_node(node_id_);
  // 内容寻址的网格可能被别的节点共用：还有引用就只摘节点、不删网格。
  bool referenced = false;
  for (const SceneNode& other : document_->scene().nodes()) {
    if (other.mesh_asset_id == node_.mesh_asset_id) {
      referenced = true;
      break;
    }
  }
  if (has_mesh_ && !referenced && !document_->tess_cache().uses_asset(node_.mesh_asset_id)) {
    document_->invalidate_geometry_lods(node_.mesh_asset_id);
    document_->remove_mesh(node_.mesh_asset_id);
  }
  document_->recompute_scene();
  document_->mark_dirty();
}

void DeleteSceneNodeCommand::undo() {
  if (!executed_) {
    return;
  }
  if (has_mesh_ && document_->mesh(node_.mesh_asset_id) == nullptr) {
    document_->insert_mesh(mesh_);
  }
  document_->scene().insert_node(node_);
  document_->recompute_scene();
  document_->mark_dirty();
}

void DeleteSceneNodeCommand::redo() {
  if (executed_) {
    remove_now();
  }
}

}  // namespace tamias
