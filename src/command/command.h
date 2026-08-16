#pragma once

#include "engine/core/result.h"
#include "engine/math/math.h"

namespace tamias {

// 可逆编辑命令的基类。
class Command {
 public:
  virtual ~Command() = default;

  [[nodiscard]] virtual Result<void> execute() = 0;
  virtual void undo() = 0;
  virtual void redo() = 0;

  // 交互式命令（如拖拽建墙）：需要交互输入（点），execute 在输入齐后由 CommandSystem 调用。
  virtual bool interactive() const { return false; }
  // 交互式命令：喂一个交互点。返回 true 表示输入齐了（可 execute）。
  virtual Result<bool> on_point(Vec3 point) {
    (void)point;
    return Err("Command is not interactive");
  }
  // 交互式命令的「起点」（供视口画预览线）。默认无起点。
  virtual bool has_start() const { return false; }
  virtual Vec3 start() const { return {}; }
};

}  // namespace tamias
