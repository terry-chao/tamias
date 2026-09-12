#pragma once

#include "bim/grid_axis.h"
#include "command/command.h"
#include "engine/document/document.h"

namespace tamias {

// 画一根轴线：方向 / 位置 / 沿轴范围一次给定。一条命令一根，可撤销。
// 交互式拖两点由 app 层换算成这些参数后调用（轴网不是构件，不走拖拽命令）。
class CreateGridAxisCommand final : public Command {
 public:
  CreateGridAxisCommand(Document& document, GridAxis axis);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t axis_id() const { return axis_.id; }

 private:
  Document* document_ = nullptr;
  GridAxis axis_;
  bool created_ = false;
};

}  // namespace tamias
