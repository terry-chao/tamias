#include "create_primitive_command.h"

#include "bim/host_update.h"
#include "entity/box_entity.h"
#include "entity/column_entity.h"
#include "entity/cylinder_entity.h"
#include "entity/door_entity.h"
#include "entity/window_entity.h"

namespace tamias {

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind)
    : document_(&document), kind_(kind) {
  if (kind_ == PrimitiveKind::Column) {
    work_plane_y_ = static_cast<float>(
        document.bim().storey_elevation(document.bim().active_storey_id()));
  }
}

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind,
                                               Vec3 position, std::uint64_t host_id)
    : CreatePrimitiveCommand(document, kind) {
  position_ = position;
  host_id_ = host_id;
  scripted_ = true;
}

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, ColumnShape shape,
                                               double size_a, double size_b, double height)
    : CreatePrimitiveCommand(document, PrimitiveKind::Column) {
  column_shape_ = shape;
  column_size_a_ = size_a;
  column_size_b_ = size_b;
  column_height_ = height;
}

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind,
                                               Vec3 position, ColumnShape shape,
                                               double size_a, double size_b, double height)
    : CreatePrimitiveCommand(document, kind, position, 0) {
  if (kind_ == PrimitiveKind::Column) {
    column_shape_ = shape;
    column_size_a_ = size_a;
    column_size_b_ = size_b;
    column_height_ = height;
  }
}

CreatePrimitiveCommand::CreatePrimitiveCommand(Document& document, PrimitiveKind kind,
                                               double width, double height, double thickness)
    : CreatePrimitiveCommand(document, kind) {
  opening_width_ = width;
  opening_height_ = height;
  opening_thickness_ = thickness;
}

Result<bool> CreatePrimitiveCommand::on_point(Vec3 point) { return on_pick(point, 0); }

Result<bool> CreatePrimitiveCommand::on_pick(Vec3 point, std::uint64_t picked_entity_id) {
  position_ = point;
  host_id_ = picked_entity_id;
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
      std::unique_ptr<ColumnEntity> column;
      if (column_shape_ == ColumnShape::Circular) {
        column = std::make_unique<ColumnEntity>(
            ColumnEntity::circular(position_, column_size_a_, column_height_));
      } else {
        column = std::make_unique<ColumnEntity>(
            position_, column_size_a_, column_size_b_, column_height_);
      }
      document_->assign_active_storey(*column);
      auto geometry = column->createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::move(column), std::move(*geometry));
      break;
    }
    case PrimitiveKind::Door: {
      DoorEntity door(position_, opening_width_, opening_height_, opening_thickness_);
      auto geometry = door.createGeom();
      if (!geometry) {
        return Err(geometry.error());
      }
      added = document_->add_entity(std::make_unique<DoorEntity>(std::move(door)),
                                    std::move(*geometry));
      break;
    }
    case PrimitiveKind::Window: {
      WindowEntity window(position_, opening_width_, opening_height_, opening_thickness_);
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

  if ((kind_ == PrimitiveKind::Window || kind_ == PrimitiveKind::Door) && host_id_ != 0) {
    if (auto r = bind_opening_to_host(*document_, added->id, host_id_, position_); r) {
      if (const Relation* rel = document_->bim().host_of(added->id)) {
        relation_ = *rel;
      }
    }
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
  if (relation_) {
    document_->bim().insert(*relation_);
    (void)notify_entity_changed(*document_, relation_->to);
  }
}

}  // namespace tamias
