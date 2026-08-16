#pragma once

#include "command/command.h"

#include <memory>
#include <vector>

namespace tamias {

// 命令栈：执行命令并压入撤销栈，支持 undo / redo。
class CommandStack {
 public:
  // 执行命令；成功则压入撤销栈并清空 redo 分支。
  void execute(std::unique_ptr<Command> command) {
    if (!command) {
      return;
    }
    if (auto r = command->execute(); r) {
      push_executed(std::move(command));
    }
  }

  // 压入一个已执行的命令（调用方已手动 execute 过，例如执行后还需上传 GPU）。
  void push_executed(std::unique_ptr<Command> command) {
    if (!command) {
      return;
    }
    undo_stack_.push_back(std::move(command));
    redo_stack_.clear();
  }

  [[nodiscard]] bool can_undo() const { return !undo_stack_.empty(); }
  [[nodiscard]] bool can_redo() const { return !redo_stack_.empty(); }

  void undo() {
    if (undo_stack_.empty()) {
      return;
    }
    auto command = std::move(undo_stack_.back());
    undo_stack_.pop_back();
    command->undo();
    redo_stack_.push_back(std::move(command));
  }

  void redo() {
    if (redo_stack_.empty()) {
      return;
    }
    auto command = std::move(redo_stack_.back());
    redo_stack_.pop_back();
    command->redo();
    undo_stack_.push_back(std::move(command));
  }

  void clear() {
    undo_stack_.clear();
    redo_stack_.clear();
  }

 private:
  std::vector<std::unique_ptr<Command>> undo_stack_;
  std::vector<std::unique_ptr<Command>> redo_stack_;
};

}  // namespace tamias
