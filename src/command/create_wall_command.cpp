#include "create_wall_command.h"

#include "entity/wall_entity.h"

namespace tamias {

CreateWallCommand::CreateWallCommand(Document& document, Vec3 start, Vec3 end, double thickness,
                                     double height)
    : document_(&document),
      start_(start),
      end_(end),
      thickness_(thickness),
      height_(height) {}

Result<void> CreateWallCommand::execute() {
  WallEntity wall = WallEntity::drag(start_, end_, thickness_, height_);  // ① drag 操作
  auto geometry = wall.createGeom();                           // ② 造型（实体 createGeom）
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_wall(std::move(wall), std::move(*geometry));  // ③ 入文档
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
