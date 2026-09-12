#pragma once

#include "bim/grid_axis.h"
#include "command/command.h"
#include "engine/document/document.h"

#include <cstddef>
#include <cstdint>

namespace tamias {

// 删一根轴线；undo 按原来的位置插回去，轴的 id 不变。
class DeleteGridAxisCommand final : public Command {
 public:
  DeleteGridAxisCommand(Document& document, std::uint64_t axis_id);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t axis_id_ = 0;
  GridAxis removed_{};
  std::size_t index_ = 0;
  bool removed_valid_ = false;
};

}  // namespace tamias
