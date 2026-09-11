#include "command/update_material_command.h"

namespace tamias {

UpdateMaterialCommand::UpdateMaterialCommand(Document& document, Material material)
    : document_(&document), incoming_(std::move(material)) {}

Result<void> UpdateMaterialCommand::execute() {
  if (incoming_.id == 0) {
    return Err("UpdateMaterialCommand: material id is 0");
  }
  const Material* existing = document_->material(incoming_.id);
  if (existing == nullptr) {
    return Err("UpdateMaterialCommand: material not found");
  }
  old_ = *existing;
  document_->insert_material(incoming_);
  document_->mark_material_users_dirty(incoming_.id);
  document_->mark_dirty();
  executed_ = true;
  return {};
}

void UpdateMaterialCommand::undo() {
  if (!executed_) {
    return;
  }
  document_->insert_material(old_);
  document_->mark_material_users_dirty(old_.id);
  document_->mark_dirty();
}

void UpdateMaterialCommand::redo() {
  if (!executed_) {
    return;
  }
  document_->insert_material(incoming_);
  document_->mark_material_users_dirty(incoming_.id);
  document_->mark_dirty();
}

}  // namespace tamias
