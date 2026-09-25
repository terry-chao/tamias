#pragma once

#include "command/core/command_system.h"
#include "engine/base/result.h"
#include "engine/document/document.h"
#include "plugin/host_api.h"
#include "plugin/plugin_command.h"
#include "plugin/plugin_info.h"
#include "plugin/plugin_pick_point.h"
#include "plugin/plugin_point_input_request.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

class CsharpRuntime;

// C ABI + C# plugin loader. App binds the active document/command system, then
// C# plugins query selection and dispatch existing commands.
class PluginHost {
 public:
  using LogSink = std::function<void(std::string_view)>;
  using AfterEdit = std::function<void()>;
  using ShowDialog = std::function<std::int32_t(std::int32_t kind, std::int32_t buttons,
                                                std::string_view spec, std::string& out)>;
  using PointInputCompletion = std::function<void(std::vector<PluginPickPoint>, bool)>;
  using BeginPointInput =
      std::function<Result<void>(PluginPointInputRequest, PointInputCompletion)>;
  using CancelPointInput = std::function<void(std::uint64_t)>;

  PluginHost();
  ~PluginHost();

  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  void set_log_sink(LogSink sink) { log_sink_ = std::move(sink); }

  void bind(Document* document, CommandSystem* command_system, AfterEdit after_edit);
  void unbind() { bind(nullptr, nullptr, {}); }
  void shutdown();
  void set_point_input_handlers(BeginPointInput begin, CancelPointInput cancel) {
    begin_point_input_ = std::move(begin);
    cancel_point_input_ = std::move(cancel);
  }
  void set_selection_changed(AfterEdit changed) { selection_changed_ = std::move(changed); }
  void set_dialog_handler(ShowDialog handler) { show_dialog_ = std::move(handler); }

  // Load Tamias.Host.dll from <exe>/managed and plugins from <exe>/plugins.
  Result<void> load();
  Result<void> invoke(std::string_view command_id);
  // 命令控制台的输入面：求值一段 C# 片段（见 Tamias.Host/ScriptEngine.cs）。
  // Ok(结果文本) / Err(错误文本)——两种都要显示，所以错误也带文本。
  [[nodiscard]] Result<std::string> evaluate(std::string_view code);

  [[nodiscard]] const std::vector<PluginCommand>& commands() const { return registered_; }
  [[nodiscard]] const std::vector<PluginInfo>& plugins() const { return plugins_; }
  [[nodiscard]] const HostApi& native_api() const { return api_; }

  Result<void> dispatch(std::string_view command, std::string_view args_text);

 private:
  static void host_log(void* context, std::int32_t level, const char* utf8);
  static std::int32_t host_document_name(void* context, char* utf8, std::int32_t cap);
  static std::int32_t host_entity_count(void* context);
  static std::int32_t host_entity_id_at(void* context, std::int32_t index, std::uint64_t* out_id);
  static std::int32_t host_entity_kind(void* context, std::uint64_t id, char* utf8, std::int32_t cap);
  static std::int32_t host_entity_name(void* context, std::uint64_t id, char* utf8, std::int32_t cap);
  static std::int32_t host_selection_count(void* context);
  static std::int32_t host_selection_id_at(void* context, std::int32_t index, std::uint64_t* out_id);
  static std::int32_t host_dispatch(void* context, const char* command, const char* args_utf8);
  static std::int32_t host_register_command(void* context, const char* id, const char* title,
                                            const char* tooltip, const char* page_id,
                                            const char* group_id, const char* icon_path,
                                            std::int32_t order, std::int32_t flags);
  static std::int32_t host_register_plugin(
      void* context, const char* id, const char* title, const char* author,
      const char* version, const char* release_date, const char* description,
      const char* homepage_url, const char* icon_path, std::int32_t flags);
  static std::int32_t host_begin_point_input(
      void* context, std::uint64_t request_id, std::int32_t min_points,
      std::int32_t max_points, std::int32_t flags, float work_plane_y,
      std::int32_t preview_kind, const char* preview_curve_kind, const char* filter_kind);
  static std::int32_t host_cancel_point_input(void* context, std::uint64_t request_id);
  static std::int32_t host_set_selection(void* context, const std::uint64_t* ids,
                                         std::int32_t count);
  static std::int32_t host_show_dialog(void* context, std::int32_t kind, std::int32_t buttons,
                                       const char* spec_utf8, char* out_utf8, std::int32_t cap);
  static std::int32_t host_entity_feature_count(void* context, std::uint64_t entity_id);
  static std::int32_t host_entity_feature_at(void* context, std::uint64_t entity_id,
                                             std::int32_t index, std::uint64_t* out_id,
                                             std::int32_t* out_kind,
                                             std::int32_t* out_input_count,
                                             std::int32_t* out_param_count);
  static std::int32_t host_feature_input_at(void* context, std::uint64_t entity_id,
                                            std::uint64_t feature_id, std::int32_t index,
                                            std::uint64_t* out_input_id);
  static std::int32_t host_feature_param_count(void* context, std::uint64_t entity_id,
                                               std::uint64_t feature_id);
  static std::int32_t host_feature_param_at(void* context, std::uint64_t entity_id,
                                            std::uint64_t feature_id, std::int32_t index,
                                            char* name_utf8, std::int32_t cap,
                                            double* out_value);
  static std::int32_t host_begin_transaction(void* context, const char* name_utf8);
  static std::int32_t host_commit_transaction(void* context);
  static std::int32_t host_abort_transaction(void* context);

  void emit_log(std::int32_t level, std::string_view message);
  // 插件忘了 commit：回滚并留一条日志。不能让一条悬着的事务把用户之后的每次编辑
  // 都吞进一个永远不会提交的缓冲里。
  void close_dangling_transaction();
  [[nodiscard]] std::vector<std::uint64_t> entity_ids() const;

  HostApi api_{};
  Document* document_ = nullptr;
  CommandSystem* command_system_ = nullptr;
  AfterEdit after_edit_;
  BeginPointInput begin_point_input_;
  CancelPointInput cancel_point_input_;
  AfterEdit selection_changed_;
  ShowDialog show_dialog_;
  LogSink log_sink_;
  std::vector<PluginCommand> registered_;
  std::vector<PluginInfo> plugins_;
  std::string current_plugin_id_;
  std::unique_ptr<CsharpRuntime> csharp_;
};

}  // namespace tamias
