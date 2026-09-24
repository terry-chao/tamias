#include "command/edit/update_text_command.h"

#include <utility>

namespace tamias {

UpdateTextCommand::UpdateTextCommand(Document& document, std::uint64_t text_id,
                                     TextAnnotation updated)
    : document_(&document), text_id_(text_id), after_(std::move(updated)) {
  after_.id = text_id_;
}

Result<void> UpdateTextCommand::execute() {
  const TextAnnotation* existing = document_->text_annotation(text_id_);
  if (existing == nullptr) {
    return Err("UpdateTextCommand: text annotation not found");
  }
  if (!captured_) {
    before_ = *existing;
    captured_ = true;
  }
  apply(after_);
  return {};
}

void UpdateTextCommand::undo() {
  if (captured_) {
    apply(before_);
  }
}

void UpdateTextCommand::redo() { apply(after_); }

void UpdateTextCommand::apply(const TextAnnotation& value) {
  TextAnnotation* target = document_->text_annotation(text_id_);
  if (target == nullptr) {
    return;
  }
  const bool selected = target->selected;  // 选中是编辑器状态，改内容别把它弄丢
  *target = value;
  target->id = text_id_;
  target->selected = selected;
}

}  // namespace tamias
