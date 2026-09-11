#include "create_curtain_wall_command.h"

#include "entity/curtain_wall_entity.h"

namespace tamias {

CreateCurtainWallCommand::CreateCurtainWallCommand(Document& document, double thickness,
                                                     double height)
    : document_(&document),
      thickness_(thickness),
      height_(height),
      elevation_(document.bim().storey_elevation(document.bim().active_storey_id())) {}

CreateCurtainWallCommand::CreateCurtainWallCommand(Document& document, double thickness,
                                                   double height, Vec3 start, Vec3 end)
    : CreateCurtainWallCommand(document, thickness, height) {
  start_ = start;
  end_ = end;
  has_start_ = true;
  scripted_ = true;
}

Result<bool> CreateCurtainWallCommand::on_point(Vec3 point) {
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;
  }
  end_ = point;
  return true;
}

Result<void> CreateCurtainWallCommand::execute() {
  CurtainWallEntity wall(start_, end_, thickness_, height_);
  document_->assign_active_storey(wall);
  auto geometry = wall.createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::make_unique<CurtainWallEntity>(std::move(wall)),
                                         std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateCurtainWallCommand: add failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateCurtainWallCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateCurtainWallCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
