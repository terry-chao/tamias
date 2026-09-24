#include "command/delete/delete_text_command.h"

#include <cstddef>

namespace tamias {

DeleteTextCommand::DeleteTextCommand(Document& document, std::uint64_t text_id)
    : document_(&document), text_id_(text_id) {}

Result<void> DeleteTextCommand::execute() {
  auto& annotations = document_->text_annotations();
  for (std::size_t i = 0; i < annotations.size(); ++i) {
    if (annotations[i].id == text_id_) {
      removed_ = annotations[i];
      index_ = i;
      removed_valid_ = true;
      annotations.erase(annotations.begin() + static_cast<std::ptrdiff_t>(i));
      return {};
    }
  }
  return Err("DeleteTextCommand: text annotation not found");
}

void DeleteTextCommand::undo() {
  if (!removed_valid_) {
    return;
  }
  auto& annotations = document_->text_annotations();
  const std::size_t at = index_ <= annotations.size() ? index_ : annotations.size();
  annotations.insert(annotations.begin() + static_cast<std::ptrdiff_t>(at), removed_);
}

void DeleteTextCommand::redo() { (void)execute(); }

}  // namespace tamias
