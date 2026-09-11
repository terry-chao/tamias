#include "delete_entity_command.h"

#include "bim/host_update.h"

namespace tamias {
namespace {

std::uint64_t opening_host_id(const std::vector<Relation>& relations, std::uint64_t entity_id) {
  for (const Relation& rel : relations) {
    if (rel.kind == RelationKind::HostedOn && rel.from == entity_id) {
      return rel.to;
    }
  }
  return 0;
}

}  // namespace

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
  const std::uint64_t host_id = opening_host_id(relations_, entity_id_);
  document_->remove_entity(entity_id_);
  if (host_id != 0) {
    (void)remesh_host_openings(*document_, host_id);
  }
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
  if (const std::uint64_t host_id = opening_host_id(relations_, entity_id_); host_id != 0) {
    (void)remesh_host_openings(*document_, host_id);
  }
}

void DeleteEntityCommand::redo() {
  const std::uint64_t host_id = opening_host_id(relations_, entity_id_);
  document_->remove_entity(entity_id_);
  if (host_id != 0) {
    (void)remesh_host_openings(*document_, host_id);
  }
}

}  // namespace tamias
