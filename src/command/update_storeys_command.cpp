#include "command/update_storeys_command.h"

namespace tamias {

Result<void> UpdateStoreysCommand::execute() {
  if (!captured_) {
    before_ = document_->bim().storeys();
    before_active_ = document_->bim().active_storey_id();
    captured_ = true;
  }
  apply(after_, after_active_);
  return {};
}

void UpdateStoreysCommand::undo() { apply(before_, before_active_); }

void UpdateStoreysCommand::redo() { apply(after_, after_active_); }

void UpdateStoreysCommand::apply(std::vector<Storey>& storeys, std::uint64_t active_storey_id) {
  document_->apply_storey_plan(storeys);
  if (active_storey_id == 0) {
    // 本来就没有当前楼层：让文档自己收尾。新建首层会被自动设为当前层；
    // 被删掉的当前层由 remove_storey 清成"未指定"。
    return;
  }
  document_->set_active_storey(document_->bim().find_storey(active_storey_id) != nullptr
                                   ? active_storey_id
                                   : 0);
}

}  // namespace tamias
