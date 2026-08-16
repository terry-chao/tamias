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
  if (auto r = command->execute(); !r) {
    return r;
  }
  stack_.push_executed(std::move(command));
  return {};
}

void CommandSystem::undo() { stack_.undo(); }
void CommandSystem::redo() { stack_.redo(); }

}  // namespace tamias
