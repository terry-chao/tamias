#pragma once

#include "command/core/command.h"
#include "command/core/command_args.h"
#include "command/core/command_group.h"
#include "command/core/command_stack.h"
#include "engine/math/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace tamias {

class Document;

// 命令执行观察者：命令**真正执行**时回调一次（含交互式命令点齐后那一次）。
// 宿主用它做命令回显（见 host/command_echo.h）；内核不知道回显长什么样。
using CommandObserver = std::function<void(const std::string& name, const CommandArgs& args)>;

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

// 启动时调用：注册所有命令（create_wall / create_column / set_param 等）。
void register_commands(CommandRegistry& registry);

// 命令系统：按名分发命令，统一管 undo 栈。每视口一份（undo 按文档隔离）。
// 交互式命令（拖拽）在 dispatch 后进入 pending，视口喂点，点齐后自动 execute + 压栈。
class CommandSystem {
 public:
  explicit CommandSystem(const CommandRegistry& registry) : registry_(registry) {}

  [[nodiscard]] Result<void> dispatch(Document& doc, const std::string& name,
                                      const CommandArgs& args);

  // ── 事务 ────────────────────────────────────────────────────────────────
  // begin 与 commit 之间成功执行的命令合成**一条**撤销记录：脚本批量改参数
  // 不该在用户面前留下 N 步撤销。
  //
  // 不支持嵌套：第二次 begin 直接报错。静默吞掉会让「谁负责 commit」变得含糊。
  [[nodiscard]] Result<void> begin_transaction(std::string name = {});
  [[nodiscard]] Result<void> commit_transaction();
  // 回滚：把事务里已执行的命令逆序撤销并丢弃，文档回到 begin 时的样子，
  // 撤销栈里**不留**记录。返回回滚的命令条数。
  [[nodiscard]] Result<std::size_t> abort_transaction();
  [[nodiscard]] bool in_transaction() const { return transaction_open_; }
  [[nodiscard]] std::size_t transaction_size() const { return transaction_.size(); }

  // 注册命令执行观察者（每个 CommandSystem 一个；重新设置会覆盖）。
  void set_observer(CommandObserver observer) { observer_ = std::move(observer); }
  // 给 pending 命令喂一个交互点；返回 true 表示命令已完成。
  [[nodiscard]] Result<bool> feed_point(Vec3 point, std::uint64_t picked_entity_id = 0);
  // 光标悬停：更新门窗等跟墙预览，不提交。
  void hover(Vec3 point, std::uint64_t picked_entity_id = 0);
  // 给 pending 命令发「确认」（折线 Enter / 双击）。返回 true 表示已完成。
  [[nodiscard]] Result<bool> confirm();
  void cancel();  // 取消 pending

  [[nodiscard]] bool has_pending() const { return pending_ != nullptr; }
  [[nodiscard]] bool drag_started() const { return pending_ && pending_->has_start(); }
  [[nodiscard]] Vec3 drag_start() const { return pending_ ? pending_->start() : Vec3{}; }
  [[nodiscard]] float work_plane_y() const {
    return pending_ ? pending_->work_plane_y() : 0.f;
  }
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
  // 清空 pending 与 undo/redo 栈（换文档时用，避免命令持有旧 Document*）。
  void clear();
  [[nodiscard]] bool can_undo() const { return stack_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return stack_.can_redo(); }
  void push_executed(std::unique_ptr<Command> command) { record_executed(std::move(command)); }

 private:
  // 事务开着就进缓冲区，否则直接进撤销栈——所有「命令已执行」的路径都走这里，
  // 免得某一条漏掉事务语义。
  void record_executed(std::unique_ptr<Command> command);
  void notify_executed(const std::string& name, const CommandArgs& args);
  // 武装参数 + 命令自己报的交互参数（点 / 宿主 id）拼成「等价的一次性调用」。
  [[nodiscard]] CommandArgs merged_echo_args() const;

  const CommandRegistry& registry_;
  CommandStack stack_;
  std::unique_ptr<Command> pending_;
  std::string pending_name_;
  CommandArgs pending_args_;  // 武装时那份参数，交互完成后与 echo_args() 合并
  CommandObserver observer_;
  bool transaction_open_ = false;
  std::string transaction_name_;
  CommandGroup transaction_;  // 事务里已执行的命令（提交时整体压栈）
};

}  // namespace tamias
