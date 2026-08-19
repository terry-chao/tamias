#include "delete_entity_command.h"

namespace tamias {

DeleteEntityCommand::DeleteEntityCommand(Document& document, std::uint64_t entity_id)
    : document_(&document), entity_id_(entity_id) {}

Result<void> DeleteEntityCommand::execute() {
  Entity* entity = document_->entity(entity_id_);
  if (entity == nullptr) {
    return Err("DeleteEntityCommand: entity not found");
  }
  const MeshAsset* mesh = document_->mesh(entity->mesh_asset_id);
  if (mesh == nullptr) {
    return Err("DeleteEntityCommand: mesh asset not found");
  }
  entity_ = entity->clone();
  mesh_ = *mesh;
  relations_.clear();
  for (const Relation& rel : document_->bim().relations()) {
    if (rel.from == entity_id_ || rel.to == entity_id_) {
      relations_.push_back(rel);
    }
  }
  document_->remove_entity(entity_id_);
  return {};
}

void DeleteEntityCommand::undo() {
  if (!entity_) {
    return;
  }
  document_->insert_entity(entity_->clone(), mesh_);
  for (const Relation& rel : relations_) {
    document_->bim().insert(rel);
  }
}

void DeleteEntityCommand::redo() { document_->remove_entity(entity_id_); }

}  // namespace tamias
