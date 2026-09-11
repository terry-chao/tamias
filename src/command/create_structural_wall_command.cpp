#include "create_structural_wall_command.h"

#include "entity/structural_wall_entity.h"

namespace tamias {

CreateStructuralWallCommand::CreateStructuralWallCommand(Document& document, double thickness,
                                                           double height)
    : document_(&document),
      thickness_(thickness),
      height_(height),
      elevation_(document.bim().storey_elevation(document.bim().active_storey_id())) {}

CreateStructuralWallCommand::CreateStructuralWallCommand(Document& document, double thickness,
                                                         double height, Vec3 start, Vec3 end)
    : CreateStructuralWallCommand(document, thickness, height) {
  start_ = start;
  end_ = end;
  has_start_ = true;
  scripted_ = true;
}

Result<bool> CreateStructuralWallCommand::on_point(Vec3 point) {
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return false;
  }
  end_ = point;
  return true;
}

Result<void> CreateStructuralWallCommand::execute() {
  StructuralWallEntity wall(start_, end_, thickness_, height_);
  document_->assign_active_storey(wall);
  auto geometry = wall.createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::make_unique<StructuralWallEntity>(std::move(wall)),
                                        std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateStructuralWallCommand: add failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateStructuralWallCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateStructuralWallCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
