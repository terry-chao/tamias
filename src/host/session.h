#pragma once

#include "command/command_system.h"
#include "engine/document/document.h"
#include "host/camera_controller.h"
#include "host/host_event.h"
#include "host/tool_mode.h"

#include <cstdint>
#include <memory>
#include <string_view>
#include <vector>

namespace tamias {

// 会话层：每文档一个。文档 + 命令系统 + 相机 + 工具模式 + 选择 + 事件。
// 不 include Qt，不碰 RenderChannel / canvas / 窗口；由壳适配器拥有。
class Session {
 public:
  explicit Session(std::shared_ptr<Document> document);
  ~Session();

  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  [[nodiscard]] Document& document() { return *document_; }
  [[nodiscard]] const Document& document() const { return *document_; }
  // 替换文档（加载文件时用），同时清空命令栈与工具状态。
  void reset_document(std::shared_ptr<Document> document);
  [[nodiscard]] CommandSystem& command_system() { return command_system_; }
  [[nodiscard]] const CommandSystem& command_system() const { return command_system_; }
  [[nodiscard]] CameraController& camera() { return camera_; }
  [[nodiscard]] const CameraController& camera() const { return camera_; }

  [[nodiscard]] ToolMode tool_mode() const { return tool_mode_; }
  void set_tool(ToolMode mode);

  [[nodiscard]] Result<void> dispatch(std::string_view name, const CommandArgs& args);
  void undo();
  void redo();
  [[nodiscard]] bool can_undo() const { return command_system_.can_undo(); }
  [[nodiscard]] bool can_redo() const { return command_system_.can_redo(); }

  // 选择（委托 Document）。
  [[nodiscard]] std::vector<std::uint64_t> selection() const { return document_->selected_ids(); }
  void select(std::uint64_t id);
  void deselect(std::uint64_t id);
  void clear_selection();
  void set_selection(const std::vector<std::uint64_t>& ids);

  void set_listener(HostListener listener) { listener_ = std::move(listener); }
  void notify(HostEvent event, std::string_view message = {});

 private:
  std::shared_ptr<Document> document_;
  CommandSystem command_system_{command_registry()};
  CameraController camera_;
  ToolMode tool_mode_ = ToolMode::None;
  HostListener listener_;
};

}  // namespace tamias
