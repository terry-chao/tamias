#include "command/create_storey_command.h"

namespace tamias {

Result<void> CreateStoreyCommand::execute() {
  storey_ = document_->add_storey(name_, elevation_);
  document_->set_active_storey(storey_.id);
  created_ = true;
  return {};
}

void CreateStoreyCommand::undo() {
  if (created_) {
    document_->remove_storey(storey_.id);
  }
}

void CreateStoreyCommand::redo() {
  if (created_) {
    document_->insert_storey(storey_);
    document_->set_active_storey(storey_.id);
  }
}

}  // namespace tamias
