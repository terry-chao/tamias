#include "create_primitive_command.h"

#include "entity/box_entity.h"
#include "entity/column_entity.h"
#include "entity/cylinder_entity.h"
#include "entity/door_entity.h"
#include "entity/slab_entity.h"
#include "entity/window_entity.h"

namespace tamias {

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind)
    : document_(&document), kind_(kind) {}

Result<bool> CreatePrimitiveCommand::on_point(Vec3 point) {
  position_ = point;
  return true;  // 单点，齐了
}

Result<void> CreatePrimitiveCommand::execute() {
  Entity* added = nullptr;
  switch (kind_) {
    case PrimitiveKind::Box: {
      BoxEntity box(position_);
      auto geometry = box.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<BoxEntity>(std::move(box)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Cylinder: {
      CylinderEntity cylinder(position_);
      auto geometry = cylinder.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<CylinderEntity>(std::move(cylinder)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Column: {
      ColumnEntity column(position_);
      auto geometry = column.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<ColumnEntity>(std::move(column)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Slab: {
      SlabEntity slab(position_);
      auto geometry = slab.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<SlabEntity>(std::move(slab)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Door: {
      DoorEntity door(position_);
      auto geometry = door.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<DoorEntity>(std::move(door)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Window: {
      WindowEntity window(position_);
      auto geometry = window.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<WindowEntity>(std::move(window)),
                                    std::move(*geometry));
      break;
    }
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
