#include "command/replace_texture_command.h"

namespace tamias {

ReplaceTextureCommand::ReplaceTextureCommand(Document& document, std::uint64_t id,
                                             TextureAsset incoming)
    : document_(&document), id_(id), incoming_(std::move(incoming)) {}

Result<void> ReplaceTextureCommand::execute() {
  const TextureAsset* existing = document_->texture(id_);
  if (existing == nullptr) {
    return Err("ReplaceTextureCommand: texture not found");
  }
  old_ = *existing;
  if (auto r = document_->replace_texture(id_, incoming_); !r) {
    return r;
  }
  const TextureAsset* updated = document_->texture(id_);
  if (updated == nullptr) {
    return Err("ReplaceTextureCommand: texture missing after replace");
  }
  new_ = *updated;
  executed_ = true;
  document_->mark_dirty();
  return {};
}

void ReplaceTextureCommand::undo() {
  if (!executed_) {
    return;
  }
  document_->insert_texture(old_);
  document_->mark_dirty();
}

void ReplaceTextureCommand::redo() {
  if (!executed_) {
    return;
  }
  document_->insert_texture(new_);
  document_->mark_dirty();
}

}  // namespace tamias
