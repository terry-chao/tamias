#pragma once

#include "command/core/command.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace tamias {

// 命令组：把多条**已经执行过**的命令当成一条撤销记录。
//
// 脚本一次改 N 个参数，用户按一次 Ctrl+Z 就该全部退回——这就是那条记录。
// undo 必须逆序：后执行的命令可能依赖先执行的结果（先建轮廓、再拉伸、再倒角）。
class CommandGroup final : public Command {
 public:
  CommandGroup() = default;
  explicit CommandGroup(std::vector<std::unique_ptr<Command>> commands)
      : commands_(std::move(commands)) {}

  void add(std::unique_ptr<Command> command) {
    if (command) {
      commands_.push_back(std::move(command));
    }
  }

  [[nodiscard]] bool empty() const { return commands_.empty(); }
  [[nodiscard]] std::size_t size() const { return commands_.size(); }

  // 交出组内命令（事务提交时把缓冲区装成一条记录）。
  std::vector<std::unique_ptr<Command>> release() { return std::move(commands_); }

  // 组是按「已经执行过」建的，没有「再执行一遍」这种用法；真被调到就是调用方搞错了。
  [[nodiscard]] Result<void> execute() override {
    return Err("CommandGroup: commands are already executed");
  }

  void undo() override {
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it) {
      (*it)->undo();
    }
  }

  void redo() override {
    for (auto& command : commands_) {
      command->redo();
    }
  }

 private:
  std::vector<std::unique_ptr<Command>> commands_;
};

}  // namespace tamias
