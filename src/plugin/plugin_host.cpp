#include "plugin/plugin_host.h"

#include "engine/core/executable_directory.h"
#include "engine/core/log.h"
#include "entity/entity.h"
#include "host/command_arg_text.h"
#include "plugin/csharp_runtime.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace tamias {
namespace {

constexpr std::int32_t kLogInfo = 0;
constexpr std::int32_t kLogWarn = 1;
constexpr std::int32_t kLogError = 2;

[[nodiscard]] std::int32_t fill_utf8(std::string_view text, char* utf8, std::int32_t cap) {
  if (utf8 == nullptr || cap <= 0) {
    return -1;
  }
  const auto n = static_cast<std::int32_t>(text.size());
  const auto copy = std::min(n, cap - 1);
  if (copy > 0) {
    std::memcpy(utf8, text.data(), static_cast<std::size_t>(copy));
  }
  utf8[copy] = '\0';
  return copy;
}

[[nodiscard]] const char* entity_kind_name(EntityKind kind) {
  switch (kind) {
    case EntityKind::Wall:
      return "Wall";
    case EntityKind::Box:
      return "Box";
    case EntityKind::Cylinder:
      return "Cylinder";
    case EntityKind::Beam:
      return "Beam";
    case EntityKind::Column:
      return "Column";
    case EntityKind::Slab:
      return "Slab";
    case EntityKind::Door:
      return "Door";
    case EntityKind::Window:
      return "Window";
    case EntityKind::Line:
      return "Line";
    case EntityKind::Polyline:
      return "Polyline";
    case EntityKind::Circle:
      return "Circle";
    case EntityKind::Arc:
      return "Arc";
    case EntityKind::Bezier:
      return "Bezier";
    case EntityKind::Rectangle:
      return "Rectangle";
  }
  return "Unknown";
}

}  // namespace

PluginHost::PluginHost() : csharp_(std::make_unique<CsharpRuntime>()) {
  api_.abi_version = kHostApiVersion;
  api_.context = this;
  api_.log = &PluginHost::host_log;
  api_.document_name = &PluginHost::host_document_name;
  api_.entity_count = &PluginHost::host_entity_count;
  api_.entity_id_at = &PluginHost::host_entity_id_at;
  api_.entity_kind = &PluginHost::host_entity_kind;
  api_.entity_name = &PluginHost::host_entity_name;
  api_.selection_count = &PluginHost::host_selection_count;
  api_.selection_id_at = &PluginHost::host_selection_id_at;
  api_.dispatch = &PluginHost::host_dispatch;
  api_.register_command = &PluginHost::host_register_command;
  api_.register_plugin = &PluginHost::host_register_plugin;
}

PluginHost::~PluginHost() = default;

void PluginHost::bind(Document* document, CommandSystem* command_system, AfterEdit after_edit) {
  document_ = document;
  command_system_ = command_system;
  after_edit_ = std::move(after_edit);
}

Result<void> PluginHost::load() {
  registered_.clear();
  plugins_.clear();
  current_plugin_id_.clear();
  const auto exe = executable_directory();
  const auto managed = exe / "managed";
  const auto plugins = exe / "plugins";
  auto started = csharp_->start(managed, plugins, &api_);
  if (!started) {
    return started;
  }
  log_info("Loaded C# plugin host from " + managed.string());
  return {};
}

Result<void> PluginHost::invoke(std::string_view command_id) {
  if (!csharp_ || !csharp_->started()) {
    return Err("C# plugin host is not loaded");
  }
  return csharp_->invoke(std::string(command_id));
}

Result<void> PluginHost::dispatch(std::string_view command, std::string_view args_text) {
  if (document_ == nullptr || command_system_ == nullptr) {
    return Err("no active document");
  }
  auto parsed = parse_command_arg_text(args_text);
  if (!parsed) {
    return Err(parsed.error());
  }
  auto r = command_system_->dispatch(*document_, std::string(command), *parsed);
  if (!r) {
    return r;
  }
  if (after_edit_) {
    after_edit_();
  }
  return {};
}

void PluginHost::emit_log(std::int32_t level, std::string_view message) {
  if (level >= kLogError) {
    log_error(message);
  } else if (level == kLogWarn) {
    log_warn(message);
  } else {
    log_info(message);
  }
  if (log_sink_) {
    log_sink_(message);
  }
}

std::vector<std::uint64_t> PluginHost::entity_ids() const {
  std::vector<std::uint64_t> ids;
  if (document_ == nullptr) {
    return ids;
  }
  ids.reserve(document_->entities().size());
  for (const auto& [id, unused] : document_->entities()) {
    (void)unused;
    ids.push_back(id);
  }
  std::sort(ids.begin(), ids.end());
  return ids;
}

void PluginHost::host_log(void* context, std::int32_t level, const char* utf8) {
  auto* self = static_cast<PluginHost*>(context);
  self->emit_log(level, utf8 != nullptr ? utf8 : "");
}

std::int32_t PluginHost::host_document_name(void* context, char* utf8, std::int32_t cap) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr) {
    return fill_utf8({}, utf8, cap);
  }
  return fill_utf8(self->document_->name(), utf8, cap);
}

std::int32_t PluginHost::host_entity_count(void* context) {
  auto* self = static_cast<PluginHost*>(context);
  return static_cast<std::int32_t>(self->entity_ids().size());
}

std::int32_t PluginHost::host_entity_id_at(void* context, std::int32_t index, std::uint64_t* out_id) {
  auto* self = static_cast<PluginHost*>(context);
  const auto ids = self->entity_ids();
  if (out_id == nullptr || index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
    return -1;
  }
  *out_id = ids[static_cast<std::size_t>(index)];
  return 0;
}

std::int32_t PluginHost::host_entity_kind(void* context, std::uint64_t id, char* utf8,
                                          std::int32_t cap) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr) {
    return -1;
  }
  const Entity* entity = self->document_->entity(id);
  if (entity == nullptr) {
    return -1;
  }
  return fill_utf8(entity_kind_name(entity->kind()), utf8, cap);
}

std::int32_t PluginHost::host_entity_name(void* context, std::uint64_t id, char* utf8,
                                          std::int32_t cap) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr) {
    return -1;
  }
  const Entity* entity = self->document_->entity(id);
  if (entity == nullptr) {
    return -1;
  }
  return fill_utf8(entity->name, utf8, cap);
}

std::int32_t PluginHost::host_selection_count(void* context) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr) {
    return 0;
  }
  return static_cast<std::int32_t>(self->document_->selected_ids().size());
}

std::int32_t PluginHost::host_selection_id_at(void* context, std::int32_t index,
                                              std::uint64_t* out_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr || out_id == nullptr) {
    return -1;
  }
  const auto ids = self->document_->selected_ids();
  if (index < 0 || static_cast<std::size_t>(index) >= ids.size()) {
    return -1;
  }
  *out_id = ids[static_cast<std::size_t>(index)];
  return 0;
}

std::int32_t PluginHost::host_dispatch(void* context, const char* command, const char* args_utf8) {
  auto* self = static_cast<PluginHost*>(context);
  auto r = self->dispatch(command != nullptr ? command : "", args_utf8 != nullptr ? args_utf8 : "");
  if (!r) {
    self->emit_log(kLogError, r.error());
    return -1;
  }
  return 0;
}

std::int32_t PluginHost::host_register_plugin(void* context, const char* id, const char* title) {
  auto* self = static_cast<PluginHost*>(context);
  if (id == nullptr || *id == '\0') {
    return -1;
  }
  PluginInfo info;
  info.id = id;
  info.title = title != nullptr && *title != '\0' ? title : id;
  for (const auto& existing : self->plugins_) {
    if (existing.id == info.id) {
      return -1;
    }
  }
  self->current_plugin_id_ = info.id;
  self->plugins_.push_back(std::move(info));
  return 0;
}

std::int32_t PluginHost::host_register_command(void* context, const char* id, const char* title,
                                               const char* tooltip) {
  auto* self = static_cast<PluginHost*>(context);
  if (id == nullptr || *id == '\0') {
    return -1;
  }
  PluginCommand cmd;
  cmd.id = id;
  cmd.title = title != nullptr && *title != '\0' ? title : id;
  cmd.tooltip = tooltip != nullptr ? tooltip : "";
  cmd.plugin_id = self->current_plugin_id_;
  for (const auto& existing : self->registered_) {
    if (existing.id == cmd.id) {
      return -1;
    }
  }
  self->registered_.push_back(std::move(cmd));
  return 0;
}

}  // namespace tamias
