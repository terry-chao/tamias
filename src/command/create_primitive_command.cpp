#include "create_primitive_command.h"

namespace tamias {

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, MeshCpu mesh, std::string name,
                                               Vec3 position)
    : document_(&document),
      cpu_mesh_(std::move(mesh)),
      name_(std::move(name)),
      position_(position) {}

Result<void> CreatePrimitiveCommand::execute() {
  MeshAsset asset{};
  asset.name = name_;
  asset.cpu = cpu_mesh_;  // 复制，保留 cpu_mesh_ 以便重复 execute
  MeshAsset& stored_mesh = document_->add_mesh(std::move(asset));
  const std::uint64_t mesh_id = stored_mesh.id;

  SceneNode node{};
  node.name = name_;
  node.mesh_asset_id = mesh_id;
  node.local_transform = translate(position_);
  SceneNode& stored_node = document_->scene().add_node(std::move(node));
  const std::uint64_t node_id = stored_node.id;

  document_->recompute_scene();
  document_->mark_dirty();

  mesh_ = *document_->mesh(mesh_id);
  node_ = *document_->scene().find(node_id);
  return {};
}

void CreatePrimitiveCommand::undo() {
  document_->scene().remove_node(node_.id);
  document_->remove_mesh(mesh_.id);
  document_->recompute_scene();
  document_->mark_dirty();
}

void CreatePrimitiveCommand::redo() {
  document_->insert_mesh(mesh_);
  document_->scene().insert_node(node_);
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
