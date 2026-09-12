#include "command/update_grid_command.h"

namespace tamias {

Result<void> UpdateGridCommand::execute() {
  if (!captured_) {
    before_ = document_->bim().grid().axes();
    captured_ = true;
  }
  apply(after_);
  return {};
}

void UpdateGridCommand::undo() { apply(before_); }

void UpdateGridCommand::redo() { apply(after_); }

void UpdateGridCommand::apply(std::vector<GridAxis>& axes) {
  document_->bim().grid().replace(axes);
  document_->bim().grid().sort_axes();
}

}  // namespace tamias
