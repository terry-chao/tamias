#pragma once

#include "engine/core/result.h"

namespace tamias {

// 可逆编辑命令的基类（骨架）。
// 命令层（command）对实体层（entity）做可撤销的修改：execute 首次执行，undo 撤销，
// redo 重做。具体命令（变换 / 改参数 / 几何操作）后续按此接口填充。
class Command {
 public:
  virtual ~Command() = default;
  [[nodiscard]] virtual Result<void> execute() = 0;
  virtual void undo() = 0;
  virtual void redo() = 0;
};

}  // namespace tamias
