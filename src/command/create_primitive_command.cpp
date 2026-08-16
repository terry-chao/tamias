#include "create_primitive_command.h"

#include "entity/box_entity.h"
#include "entity/cylinder_entity.h"

namespace tamias {

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind)
    : document_(&document), kind_(kind) {}

Result<bool> CreatePrimitiveCommand::on_point(Vec3 point) {
  position_ = point;
  return true;  // 单点，齐了
}

Result<void> CreatePrimitiveCommand::execute() {
  Entity* added = nullptr;
  if (kind_ == PrimitiveKind::Box) {
    BoxEntity box(position_);            // 建实体
    auto geometry = box.createGeom();    // 造型
    if (!geometry) {
      return Err(geometry.error());
    }
    added = document_->add_box(std::move(box), std::move(*geometry));  // 入文档
  } else {
    CylinderEntity cylinder(position_);
    auto geometry = cylinder.createGeom();
    if (!geometry) {
      return Err(geometry.error());
    }
    added = document_->add_cylinder(std::move(cylinder), std::move(*geometry));
  }
  if (added == nullptr) {
    return Err("CreatePrimitiveCommand: add primitive failed");
  }
  entity_ = added->clone();
  if (const MeshAsset* mesh = document_->mesh(added->mesh_asset_id)) {
    mesh_ = *mesh;
  }
  return {};
}

void CreatePrimitiveCommand::undo() {
  if (entity_) {
    document_->remove_entity(entity_->id);
  }
}

void CreatePrimitiveCommand::redo() {
  if (entity_) {
    document_->insert_entity(entity_->clone(), mesh_);
  }
}

}  // namespace tamias
