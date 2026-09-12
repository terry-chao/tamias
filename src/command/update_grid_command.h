#pragma once

#include "bim/grid_axis.h"
#include "command/command.h"
#include "engine/document/document.h"

#include <vector>

namespace tamias {

// 轴网设置：一次替换整张轴网表（增 / 删 / 改名 / 改位置 / 改范围），一步撤销。
// 表里 id == 0 的条目按新增处理，首次执行时分配 id 并写回 after_，redo 复用同一批句柄。
class UpdateGridCommand final : public Command {
 public:
  UpdateGridCommand(Document& document, std::vector<GridAxis> axes)
      : document_(&document), after_(std::move(axes)) {}

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  void apply(std::vector<GridAxis>& axes);

  Document* document_ = nullptr;
  std::vector<GridAxis> after_;
  std::vector<GridAxis> before_;
  bool captured_ = false;
};

}  // namespace tamias
