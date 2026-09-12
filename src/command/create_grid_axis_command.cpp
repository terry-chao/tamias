#include "command/create_grid_axis_command.h"

#include <utility>

namespace tamias {

CreateGridAxisCommand::CreateGridAxisCommand(Document& document, GridAxis axis)
    : document_(&document), axis_(std::move(axis)) {}

Result<void> CreateGridAxisCommand::execute() {
  const GridAxis& added = document_->bim().grid().insert(axis_);
  axis_ = added;
  created_ = true;
  return {};
}

void CreateGridAxisCommand::undo() {
  if (created_) {
    document_->bim().grid().remove(axis_.id);
  }
}

void CreateGridAxisCommand::redo() {
  if (created_) {
    document_->bim().grid().insert(axis_);
  }
}

}  // namespace tamias
