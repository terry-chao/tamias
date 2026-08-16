#include "set_material_command.h"

namespace tamias {

SetMaterialCommand::SetMaterialCommand(Document& document, std::uint64_t entity_id,
                                       Material material)
    : document_(&document), entity_id_(entity_id), material_(std::move(material)) {}

Result<void> SetMaterialCommand::execute() {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("SetMaterialCommand: entity not found");
  }
  old_material_id_ = entity->material_id;

  // id != 0 且已存在 → 引用现有材质；否则新建入库（Document 分配新 id）。
  if (material_.id != 0 && document_->material(material_.id) != nullptr) {
    new_material_id_ = material_.id;
  } else {
    Material fresh = material_;
    fresh.id = 0;  // 交给 Document 分配
    new_material_id_ = document_->add_material(std::move(fresh)).id;
  }

  entity->material_id = new_material_id_;
  document_->mark_dirty();
  return {};
}

void SetMaterialCommand::undo() {
  if (Entity* entity = document_->entity(entity_id_)) {
    entity->material_id = old_material_id_;
    document_->mark_dirty();
  }
}

void SetMaterialCommand::redo() {
  if (Entity* entity = document_->entity(entity_id_)) {
    entity->material_id = new_material_id_;
    document_->mark_dirty();
  }
}

}  // namespace tamias
