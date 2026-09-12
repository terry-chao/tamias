#include "create_foundation_command.h"

#include "entity/structural/foundation_entity.h"

namespace tamias {

CreateFoundationCommand::CreateFoundationCommand(Document& document, double length,
                                                   double width, double height)
    : document_(&document), length_(length), width_(width), height_(height) {
  work_plane_y_ = static_cast<float>(
      document.bim().storey_elevation(document.bim().active_storey_id()));
}

CreateFoundationCommand::CreateFoundationCommand(Document& document, double diameter,
                                                   double height)
    : document_(&document), shape_(FoundationShape::Pile), diameter_(diameter), height_(height) {
  work_plane_y_ = static_cast<float>(
      document.bim().storey_elevation(document.bim().active_storey_id()));
}

CreateFoundationCommand::CreateFoundationCommand(Document& document, double length,
                                                   double width, double height, Vec3 position)
    : CreateFoundationCommand(document, length, width, height) {
  position_ = position;
  scripted_ = true;
}

Result<bool> CreateFoundationCommand::on_point(Vec3 point) {
  position_ = point;
  return true;
}

Result<void> CreateFoundationCommand::execute() {
  std::unique_ptr<FoundationEntity> footing;
  if (shape_ == FoundationShape::Pile) {
    footing = std::make_unique<FoundationEntity>(FoundationEntity::pile(position_, diameter_,
                                                                            height_));
  } else {
    footing = std::make_unique<FoundationEntity>(position_, length_, width_, height_);
  }
  document_->assign_active_storey(*footing);
  auto geometry = footing->createGeom();
  if (!geometry) {
    return Err(geometry.error());
  }
  Entity* added = document_->add_entity(std::move(footing), std::move(*geometry));
  if (added == nullptr) {
    return Err("CreateFoundationCommand: add failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreateFoundationCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreateFoundationCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
