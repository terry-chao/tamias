#include "create_wall_command.h"

#include "entity/wall_entity.h"

namespace tamias {

CreateWallCommand::CreateWallCommand(Document& document, double thickness, double height)
    : document_(&document),
      thickness_(thickness),
      height_(height),
      elevation_(document.bim().storey_elevation(document.bim().active_storey_id())) {}

Result<bool> CreateWallCommand::on_point(Vec3 point) {
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;  // 第一点，还没完
  }
  end_ = point;
  return true;  // 第二点，齐了
}

Result<void> CreateWallCommand::execute() {
  WallEntity wall(start_, end_, thickness_, height_);  // 两点构造实体
  document_->assign_active_storey(wall);
  auto geometry = wall.createGeom();                    // 造型（实体 createGeom）
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::make_unique<WallEntity>(std::move(wall)),
                                        std::move(*geometry));  // 入文档
  if (added == nullptr) {
    return Err("CreateWallCommand: add_wall failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateWallCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateWallCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
