#include "command/command_system.h"

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
    pending_ = std::move(command);  // 武装，等待交互点
    return {};
  }
  if (auto r = command->execute(); !r) {
    return r;
  }
  stack_.push_executed(std::move(command));
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
    if (auto r = pending_->execute(); !r) {
      pending_.reset();
      return Err(r.error());
    }
    stack_.push_executed(std::move(pending_));
    pending_.reset();
    return true;  // 完成
  }
  return false;  // 还没完
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
    if (auto r = pending_->execute(); !r) {
      pending_.reset();
      return Err(r.error());
    }
    stack_.push_executed(std::move(pending_));
    pending_.reset();
    return true;
  }
  return false;
}

void CommandSystem::cancel() { pending_.reset(); }

void CommandSystem::undo() { stack_.undo(); }
void CommandSystem::redo() { stack_.redo(); }

void CommandSystem::clear() {
  pending_.reset();
  stack_.clear();
}

}  // namespace tamias
