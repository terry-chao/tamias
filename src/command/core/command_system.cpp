#include "command/core/command_system.h"

#include "engine/profile/timing_scope.h"

namespace tamias {

void CommandRegistry::register_command(std::string name, Factory factory) {
  registry_[std::move(name)] = std::move(factory);
}

std::unique_ptr<Command> CommandRegistry::create(const std::string& name, Document& doc,
                                                 const CommandArgs& args) const {
  auto it = registry_.find(name);
  if (it == registry_.end()) {
    return nullptr;
  }
  return it->second(doc, args);
}

CommandRegistry& command_registry() {
  static CommandRegistry registry;
  return registry;
}

Result<void> CommandSystem::dispatch(Document& doc, const std::string& name,
                                     const CommandArgs& args) {
  auto command = registry_.create(name, doc, args);
  if (!command) {
    return Err("CommandSystem: unknown command '" + name + "'");
  }
  if (command->interactive()) {
    if (transaction_open_) {
      // 交互式命令点齐的时刻由鼠标决定，不在事务窗口里——拒绝比默默错位好。
      return Err("CommandSystem: cannot arm interactive command '" + name +
                 "' inside a transaction");
    }
    pending_ = std::move(command);  // 武装，等待交互点
    pending_name_ = name;
    pending_args_ = args;
    return {};
  }
  TAMIAS_TIMING_SCOPE_DYNAMIC(name, TimingCategory::Command);
  if (auto r = command->execute(); !r) {
    return r;
  }
  record_executed(std::move(command));
  notify_executed(name, args);
  return {};
}

Result<bool> CommandSystem::feed_point(Vec3 point, std::uint64_t picked_entity_id) {
  if (!pending_) {
    return Err("CommandSystem: no pending command");
  }
  auto done = pending_->on_pick(point, picked_entity_id);
  if (!done) {
    return Err(done.error());
  }
  if (*done) {
    TAMIAS_TIMING_SCOPE_DYNAMIC(pending_name_, TimingCategory::Command);
    if (auto r = pending_->execute(); !r) {
      cancel();
      return Err(r.error());
    }
    const std::string name = pending_name_;
    CommandArgs echoed = merged_echo_args();
    record_executed(std::move(pending_));
    pending_name_.clear();
    pending_args_.clear();
    notify_executed(name, echoed);
    return true;  // 完成
  }
  return false;  // 还没完
}

void CommandSystem::hover(Vec3 point, std::uint64_t picked_entity_id) {
  if (pending_) {
    pending_->on_hover(point, picked_entity_id);
  }
}

Result<bool> CommandSystem::confirm() {
  if (!pending_) {
    return Err("CommandSystem: no pending command");
  }
  auto done = pending_->on_confirm();
  if (!done) {
    return Err(done.error());
  }
  if (*done) {
    TAMIAS_TIMING_SCOPE_DYNAMIC(pending_name_, TimingCategory::Command);
    if (auto r = pending_->execute(); !r) {
      cancel();
      return Err(r.error());
    }
    const std::string name = pending_name_;
    CommandArgs echoed = merged_echo_args();
    record_executed(std::move(pending_));
    pending_name_.clear();
    pending_args_.clear();
    notify_executed(name, echoed);
    return true;
  }
  return false;
}

void CommandSystem::cancel() {
  pending_.reset();
  pending_name_.clear();
  pending_args_.clear();
}

void CommandSystem::undo() {
  TAMIAS_TIMING_SCOPE("undo", TimingCategory::Command);
  stack_.undo();
}

void CommandSystem::redo() {
  TAMIAS_TIMING_SCOPE("redo", TimingCategory::Command);
  stack_.redo();
}

void CommandSystem::clear() {
  cancel();
  transaction_open_ = false;
  transaction_name_.clear();
  transaction_ = CommandGroup{};  // 文档要换了，别对着旧文档 undo
  stack_.clear();
}

Result<void> CommandSystem::begin_transaction(std::string name) {
  if (transaction_open_) {
    return Err("CommandSystem: transaction already open (no nesting)");
  }
  transaction_open_ = true;
  transaction_name_ = std::move(name);
  transaction_ = CommandGroup{};
  return {};
}

Result<void> CommandSystem::commit_transaction() {
  if (!transaction_open_) {
    return Err("CommandSystem: no open transaction");
  }
  transaction_open_ = false;
  transaction_name_.clear();
  auto commands = transaction_.release();
  if (commands.empty()) {
    return {};  // 空事务不产生撤销记录
  }
  stack_.push_executed(std::make_unique<CommandGroup>(std::move(commands)));
  return {};
}

Result<std::size_t> CommandSystem::abort_transaction() {
  if (!transaction_open_) {
    return Err("CommandSystem: no open transaction");
  }
  transaction_open_ = false;
  transaction_name_.clear();
  auto commands = transaction_.release();
  const std::size_t count = commands.size();
  CommandGroup rollback(std::move(commands));
  rollback.undo();  // 逆序退回去，然后整组丢掉：栈里不留记录
  return count;
}

void CommandSystem::record_executed(std::unique_ptr<Command> command) {
  if (command == nullptr) {
    return;
  }
  if (transaction_open_) {
    transaction_.add(std::move(command));
    return;
  }
  stack_.push_executed(std::move(command));
}

void CommandSystem::notify_executed(const std::string& name, const CommandArgs& args) {
  if (observer_) {
    observer_(name, args);
  }
}

CommandArgs CommandSystem::merged_echo_args() const {
  CommandArgs merged = pending_args_;
  if (pending_ != nullptr) {
    for (auto& [key, value] : pending_->echo_args()) {
      merged[key] = std::move(value);
    }
  }
  return merged;
}

}  // namespace tamias
