#include "command/import_texture_command.h"

namespace tamias {

ImportTextureCommand::ImportTextureCommand(Document& document, TextureAsset asset)
    : document_(&document), incoming_(std::move(asset)) {}

Result<void> ImportTextureCommand::execute() {
  if (incoming_.content_hash == 0) {
    incoming_.content_hash = texture_content_hash(incoming_);
  }
  added_ = document_->texture_by_hash(incoming_.content_hash) == nullptr;
  stored_ = document_->import_texture(incoming_);
  id_ = stored_.id;
  document_->mark_dirty();
  return {};
}

void ImportTextureCommand::undo() {
  if (!added_ || id_ == 0) {
    return;
  }
  document_->remove_texture(id_);
  document_->mark_dirty();
}

void ImportTextureCommand::redo() {
  if (!added_) {
    return;
  }
  document_->insert_texture(stored_);
  document_->mark_dirty();
}

}  // namespace tamias
