#pragma once

#include "command/command_system.h"
#include "engine/core/result.h"
#include "engine/document/document.h"
#include "plugin/host_api.h"
#include "plugin/plugin_command.h"
#include "plugin/plugin_info.h"

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

  PluginHost();
  ~PluginHost();

  PluginHost(const PluginHost&) = delete;
  PluginHost& operator=(const PluginHost&) = delete;

  void set_log_sink(LogSink sink) { log_sink_ = std::move(sink); }

  void bind(Document* document, CommandSystem* command_system, AfterEdit after_edit);
  void unbind() { bind(nullptr, nullptr, {}); }

  // Load Tamias.Host.dll from <exe>/managed and plugins from <exe>/plugins.
  Result<void> load();
  Result<void> invoke(std::string_view command_id);

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
                                            const char* tooltip);
  static std::int32_t host_register_plugin(void* context, const char* id, const char* title);

  void emit_log(std::int32_t level, std::string_view message);
  [[nodiscard]] std::vector<std::uint64_t> entity_ids() const;

  HostApi api_{};
  Document* document_ = nullptr;
  CommandSystem* command_system_ = nullptr;
  AfterEdit after_edit_;
  LogSink log_sink_;
  std::vector<PluginCommand> registered_;
  std::vector<PluginInfo> plugins_;
  std::string current_plugin_id_;
  std::unique_ptr<CsharpRuntime> csharp_;
};

}  // namespace tamias
