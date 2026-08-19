#pragma once

#include "command/command.h"
#include "command/command_stack.h"
#include "engine/math/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

namespace tamias {

class Document;

// 命令参数：异构值（数值 / 整数 id / 向量 / 字符串）。
using CommandArg = std::variant<double, std::int64_t, Vec3, std::string>;
using CommandArgs = std::unordered_map<std::string, CommandArg>;

// 命令注册表：名字 → 工厂。全局单例，启动时加载一次。
class CommandRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Command>(Document&, const CommandArgs&)>;

  void register_command(std::string name, Factory factory);
  [[nodiscard]] std::unique_ptr<Command> create(const std::string& name, Document& doc,
                                                const CommandArgs& args) const;

 private:
  std::unordered_map<std::string, Factory> registry_;
};

[[nodiscard]] CommandRegistry& command_registry();

// 启动时调用：注册所有命令（create_wall / create_box / create_cylinder / set_param）。
void register_commands(CommandRegistry& registry);

// 命令系统：按名分发命令，统一管 undo 栈。每视口一份（undo 按文档隔离）。
// 交互式命令（拖拽）在 dispatch 后进入 pending，视口喂点，点齐后自动 execute + 压栈。
class CommandSystem {
 public:
  explicit CommandSystem(const CommandRegistry& registry) : registry_(registry) {}

  [[nodiscard]] Result<void> dispatch(Document& doc, const std::string& name,
                                      const CommandArgs& args);
  // 给 pending 命令喂一个交互点；返回 true 表示命令已完成。
  [[nodiscard]] Result<bool> feed_point(Vec3 point, std::uint64_t picked_entity_id = 0);
  // 给 pending 命令发「确认」（折线 Enter / 双击）。返回 true 表示已完成。
  [[nodiscard]] Result<bool> confirm();
  void cancel();  // 取消 pending

  [[nodiscard]] bool has_pending() const { return pending_ != nullptr; }
  [[nodiscard]] bool drag_started() const { return pending_ && pending_->has_start(); }
  [[nodiscard]] Vec3 drag_start() const { return pending_ ? pending_->start() : Vec3{}; }
  [[nodiscard]] bool accepts_confirm() const { return pending_ && pending_->accepts_confirm(); }
  [[nodiscard]] std::vector<Vec3> preview_polyline(Vec3 cursor) const {
    return pending_ ? pending_->preview_polyline(cursor) : std::vector<Vec3>{};
  }
  [[nodiscard]] std::vector<Vec3> preview_control_polyline(Vec3 cursor) const {
    return pending_ ? pending_->preview_control_polyline(cursor) : std::vector<Vec3>{};
  }
  [[nodiscard]] std::vector<Vec3> preview_points(Vec3 cursor) const {
    return pending_ ? pending_->preview_points(cursor) : std::vector<Vec3>{};
  }

  void undo();
  void redo();
  [[nodiscard]] bool can_undo() const { return stack_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return stack_.can_redo(); }

 private:
  const CommandRegistry& registry_;
  CommandStack stack_;
  std::unique_ptr<Command> pending_;
};

}  // namespace tamias
