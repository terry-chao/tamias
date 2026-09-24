#include "command/create/create_text_command.h"

#include <utility>

namespace tamias {

CreateTextCommand::CreateTextCommand(Document& document, TextAnnotation annotation)
    : document_(&document), annotation_(std::move(annotation)) {}

Result<void> CreateTextCommand::execute() {
  annotation_ = document_->add_text_annotation(annotation_);  // 拿回分配好的 id
  created_ = true;
  return {};
}

void CreateTextCommand::undo() {
  if (created_) {
    document_->remove_text_annotation(annotation_.id);
  }
}

void CreateTextCommand::redo() {
  if (created_) {
    document_->insert_text_annotation(annotation_);  // 保留原 id，选择 / 引用不跳
  }
}

}  // namespace tamias
