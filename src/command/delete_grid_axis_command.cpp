#include "command/delete_grid_axis_command.h"

#include <cstddef>

namespace tamias {

DeleteGridAxisCommand::DeleteGridAxisCommand(Document& document, std::uint64_t axis_id)
    : document_(&document), axis_id_(axis_id) {}

Result<void> DeleteGridAxisCommand::execute() {
  auto& axes = document_->bim().grid().axes();
  for (std::size_t i = 0; i < axes.size(); ++i) {
    if (axes[i].id == axis_id_) {
      removed_ = axes[i];
      index_ = i;
      removed_valid_ = true;
      axes.erase(axes.begin() + static_cast<std::ptrdiff_t>(i));
      return {};
    }
  }
  return Err("DeleteGridAxisCommand: axis not found");
}

void DeleteGridAxisCommand::undo() {
  if (!removed_valid_) {
    return;
  }
  auto& axes = document_->bim().grid().axes();
  const std::size_t at = index_ <= axes.size() ? index_ : axes.size();
  axes.insert(axes.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

void DeleteGridAxisCommand::redo() {
  auto& axes = document_->bim().grid().axes();
  for (std::size_t i = 0; i < axes.size(); ++i) {
    if (axes[i].id == axis_id_) {
      axes.erase(axes.begin() + static_cast<std::ptrdiff_t>(i));
      return;
    }
  }
}

}  // namespace tamias
