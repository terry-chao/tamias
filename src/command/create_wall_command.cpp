#include "create_wall_command.h"

#include "engine/graphics/mesh.h"

#include <algorithm>
#include <cmath>

namespace tamias {

CreateWallCommand::CreateWallCommand(Document& document, Vec3 start, Vec3 end, double thickness,
                                     double height)
    : document_(&document),
      start_(start),
      end_(end),
      thickness_(thickness),
      height_(height) {}

Result<void> CreateWallCommand::execute() {
  const Vec3 d = end_ - start_;
  const float length = std::max(std::sqrt(d.x * d.x + d.z * d.z), 1e-3f);
  const Vec3 mid = (start_ + end_) * 0.5f;
  const float yaw = std::atan2(d.x, d.z);

  MeshCpu box = make_box_mesh(static_cast<float>(thickness_), static_cast<float>(height_), length);

  MeshAsset asset{};
  asset.name = "wall";
  asset.cpu = std::move(box);
  MeshAsset& stored_mesh = document_->add_mesh(std::move(asset));
  const std::uint64_t mesh_id = stored_mesh.id;

  SceneNode node{};
  node.name = "wall";
  node.mesh_asset_id = mesh_id;
  node.local_transform = translate(mid) * rotate_y(yaw);
  SceneNode& stored_node = document_->scene().add_node(std::move(node));
  const std::uint64_t node_id = stored_node.id;

  document_->recompute_scene();
  document_->mark_dirty();

  // 缓存创建结果，供 undo / redo 用。
  mesh_ = *document_->mesh(mesh_id);
  node_ = *document_->scene().find(node_id);
  return {};
}

void CreateWallCommand::undo() {
  document_->scene().remove_node(node_.id);
  document_->remove_mesh(mesh_.id);
  document_->recompute_scene();
  document_->mark_dirty();
}

void CreateWallCommand::redo() {
  document_->insert_mesh(mesh_);
  document_->scene().insert_node(node_);
  document_->recompute_scene();
  document_->mark_dirty();
}

}  // namespace tamias
