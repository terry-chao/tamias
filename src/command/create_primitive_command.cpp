#include "create_primitive_command.h"

#include "entity/box_entity.h"
#include "entity/cylinder_entity.h"

namespace tamias {

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind,
                                               Vec3 position)
    : document_(&document), kind_(kind), position_(position) {}

Result<void> CreatePrimitiveCommand::execute() {
  Entity* added = nullptr;
  if (kind_ == PrimitiveKind::Box) {
    BoxEntity box = BoxEntity::at(position_);  // ① 建实体
    auto geometry = box.createGeom();          // ② 造型
    if (!geometry) {
      return Err(geometry.error());
    }
    added = document_->add_box(std::move(box), std::move(*geometry));  // ③ 入文档
  } else {
    CylinderEntity cylinder = CylinderEntity::at(position_);
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
