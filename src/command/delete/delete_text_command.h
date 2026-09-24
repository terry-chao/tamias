#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/document/text_annotation.h"

#include <cstddef>
#include <cstdint>

namespace tamias {

// 删一段文字注记；undo 按原来的位置插回去，id 不变。
class DeleteTextCommand final : public Command {
 public:
  DeleteTextCommand(Document& document, std::uint64_t text_id);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t text_id_ = 0;
  TextAnnotation removed_{};
  std::size_t index_ = 0;
  bool removed_valid_ = false;
};

}  // namespace tamias
