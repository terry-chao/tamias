#include "plugin/plugin_host.h"

#include "engine/base/executable_directory.h"
#include "engine/base/fs_utf8.h"
#include "engine/base/log.h"
#include "entity/core/entity.h"
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

// 参数按键升序：model 里的 params 是 unordered_map，没有稳定顺序。
// 插件按下标取参数时要的是「确定的第 N 个」，所以每次取都排一遍（参数只有几个）。
[[nodiscard]] std::vector<const std::pair<const std::string, double>*> sorted_params(
    const Feature& feature) {
  std::vector<const std::pair<const std::string, double>*> ordered;
  ordered.reserve(feature.params.size());
  for (const auto& entry : feature.params) {
    ordered.push_back(&entry);
  }
  std::sort(ordered.begin(), ordered.end(),
            [](const auto* a, const auto* b) { return a->first < b->first; });
  return ordered;
}

// 宽读的公共入口：文档 / 实体 / 特征三级都要在，否则 -1。
[[nodiscard]] const Feature* find_feature(const Document* document, std::uint64_t entity_id,
                                          std::uint64_t feature_id) {
  if (document == nullptr || feature_id == 0) {
    return nullptr;
  }
  const Entity* entity = document->entity(entity_id);
  return entity == nullptr ? nullptr : entity->model.find(feature_id);
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
  api_.begin_point_input = &PluginHost::host_begin_point_input;
  api_.cancel_point_input = &PluginHost::host_cancel_point_input;
  api_.set_selection = &PluginHost::host_set_selection;
  api_.show_dialog = &PluginHost::host_show_dialog;
  api_.entity_feature_count = &PluginHost::host_entity_feature_count;
  api_.entity_feature_at = &PluginHost::host_entity_feature_at;
  api_.feature_input_at = &PluginHost::host_feature_input_at;
  api_.feature_param_count = &PluginHost::host_feature_param_count;
  api_.feature_param_at = &PluginHost::host_feature_param_at;
  api_.begin_transaction = &PluginHost::host_begin_transaction;
  api_.commit_transaction = &PluginHost::host_commit_transaction;
  api_.abort_transaction = &PluginHost::host_abort_transaction;
  api_.unregister_plugin = &PluginHost::host_unregister_plugin;
}

PluginHost::~PluginHost() { shutdown(); }

void PluginHost::shutdown() {
  unbind();
  begin_point_input_ = {};
  cancel_point_input_ = {};
  selection_changed_ = {};
  show_dialog_ = {};
  log_sink_ = {};
  if (csharp_) {
    csharp_->shutdown();
  }
}

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
  // 根目录按顺序排：内置在前、用户在后——用户装的同 id 扩展盖掉内置的。
  auto roots = extension_roots_;
  if (roots.empty()) {
    roots.push_back(exe / "plugins");
  }
  std::string joined;
  for (const auto& root : roots) {
    if (!joined.empty()) {
      joined += '\n';  // 路径里不会出现换行，比分号安全
    }
    joined += path_to_utf8(root);  // 用户名里有中文也不能乱码
  }
  auto started = csharp_->start(managed, joined, &api_);
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
  auto result = csharp_->invoke(std::string(command_id));
  close_dangling_transaction();
  return result;
}

Result<std::string> PluginHost::reload_extensions() {
  if (!csharp_ || !csharp_->started()) {
    return Err("C# host is not loaded (the .NET runtime is required)");
  }
  auto summary = csharp_->reload();
  if (!summary) {
    return Err(summary.error());
  }
  return *summary;
}

Result<std::string> PluginHost::evaluate(std::string_view code) {
  if (!csharp_ || !csharp_->started()) {
    return Err("C# host is not loaded (the console needs the .NET runtime)");
  }
  if (document_ == nullptr || command_system_ == nullptr) {
    return Err("no active document");
  }
  auto result = csharp_->evaluate(code);
  // 脚本自己也可能留下悬空事务（没 commit）：和插件命令一样，收尾时回滚掉。
  close_dangling_transaction();
  if (!result) {
    return Err(result.error());
  }
  if (after_edit_) {
    after_edit_();  // 脚本里可能 dispatch 过命令，壳要刷新
  }
  return *result;
}

void PluginHost::close_dangling_transaction() {
  if (command_system_ == nullptr || !command_system_->in_transaction()) {
    return;
  }
  auto rolled = command_system_->abort_transaction();
  const std::size_t count = rolled ? *rolled : 0;
  emit_log(kLogWarn, "plugin left a transaction open; rolled back " + std::to_string(count) +
                         " command(s)");
  if (count > 0 && after_edit_) {
    after_edit_();
  }
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

std::int32_t PluginHost::host_entity_feature_count(void* context, std::uint64_t entity_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || self->document_ == nullptr) {
    return -1;
  }
  const Entity* entity = self->document_->entity(entity_id);
  if (entity == nullptr) {
    return -1;
  }
  return static_cast<std::int32_t>(entity->model.features().size());
}

std::int32_t PluginHost::host_entity_feature_at(void* context, std::uint64_t entity_id,
                                                std::int32_t index, std::uint64_t* out_id,
                                                std::int32_t* out_kind,
                                                std::int32_t* out_input_count,
                                                std::int32_t* out_param_count) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || self->document_ == nullptr || index < 0) {
    return -1;
  }
  const Entity* entity = self->document_->entity(entity_id);
  if (entity == nullptr) {
    return -1;
  }
  const auto& features = entity->model.features();
  if (static_cast<std::size_t>(index) >= features.size()) {
    return -1;
  }
  const Feature& feature = features[static_cast<std::size_t>(index)];
  if (out_id != nullptr) {
    *out_id = feature.id;
  }
  if (out_kind != nullptr) {
    *out_kind = static_cast<std::int32_t>(feature.kind);
  }
  if (out_input_count != nullptr) {
    *out_input_count = static_cast<std::int32_t>(feature.inputs.size());
  }
  if (out_param_count != nullptr) {
    *out_param_count = static_cast<std::int32_t>(feature.params.size());
  }
  return 0;
}

std::int32_t PluginHost::host_feature_input_at(void* context, std::uint64_t entity_id,
                                               std::uint64_t feature_id, std::int32_t index,
                                               std::uint64_t* out_input_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || out_input_id == nullptr || index < 0) {
    return -1;
  }
  const Feature* feature = find_feature(self->document_, entity_id, feature_id);
  if (feature == nullptr || static_cast<std::size_t>(index) >= feature->inputs.size()) {
    return -1;
  }
  *out_input_id = feature->inputs[static_cast<std::size_t>(index)];
  return 0;
}

std::int32_t PluginHost::host_feature_param_count(void* context, std::uint64_t entity_id,
                                                  std::uint64_t feature_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr) {
    return -1;
  }
  const Feature* feature = find_feature(self->document_, entity_id, feature_id);
  return feature == nullptr ? -1 : static_cast<std::int32_t>(feature->params.size());
}

std::int32_t PluginHost::host_feature_param_at(void* context, std::uint64_t entity_id,
                                               std::uint64_t feature_id, std::int32_t index,
                                               char* name_utf8, std::int32_t cap,
                                               double* out_value) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || index < 0) {
    return -1;
  }
  const Feature* feature = find_feature(self->document_, entity_id, feature_id);
  if (feature == nullptr) {
    return -1;
  }
  const auto ordered = sorted_params(*feature);
  if (static_cast<std::size_t>(index) >= ordered.size()) {
    return -1;
  }
  const auto* entry = ordered[static_cast<std::size_t>(index)];
  if (out_value != nullptr) {
    *out_value = entry->second;
  }
  if (name_utf8 == nullptr || cap <= 0) {
    return 0;  // 只要值的时候可以不给名字缓冲
  }
  return fill_utf8(entry->first, name_utf8, cap);
}

std::int32_t PluginHost::host_begin_transaction(void* context, const char* name_utf8) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || self->command_system_ == nullptr) {
    if (self != nullptr) {
      self->emit_log(kLogError, "no active document");
    }
    return -1;
  }
  auto r = self->command_system_->begin_transaction(name_utf8 != nullptr ? name_utf8 : "");
  if (!r) {
    self->emit_log(kLogError, r.error());
    return -1;
  }
  return 0;
}

std::int32_t PluginHost::host_commit_transaction(void* context) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || self->command_system_ == nullptr) {
    if (self != nullptr) {
      self->emit_log(kLogError, "no active document");
    }
    return -1;
  }
  auto r = self->command_system_->commit_transaction();
  if (!r) {
    self->emit_log(kLogError, r.error());
    return -1;
  }
  // 事务里的命令各自 dispatch 时已经刷过一次；这里再刷一次是因为撤销记录的粒度
  // 变了（面板上的「可撤销」状态要跟着变），成本可以忽略。
  if (self->after_edit_) {
    self->after_edit_();
  }
  return 0;
}

std::int32_t PluginHost::host_abort_transaction(void* context) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || self->command_system_ == nullptr) {
    if (self != nullptr) {
      self->emit_log(kLogError, "no active document");
    }
    return -1;
  }
  auto r = self->command_system_->abort_transaction();
  if (!r) {
    self->emit_log(kLogError, r.error());
    return -1;
  }
  // 回滚把文档改回去了：视口 / 网格 / BVH 都得重来一遍。
  if (*r > 0 && self->after_edit_) {
    self->after_edit_();
  }
  return static_cast<std::int32_t>(*r);
}

// 摘掉一个扩展：它的命令 + 它的元数据。返回摘掉了几条（命令 + 插件记录）。
std::int32_t PluginHost::host_unregister_plugin(void* context, const char* plugin_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (self == nullptr || plugin_id == nullptr || *plugin_id == '\0') {
    return -1;
  }
  const std::string id(plugin_id);
  const auto commands = std::erase_if(self->registered_, [&id](const PluginCommand& command) {
    return command.plugin_id == id;
  });
  const auto plugins = std::erase_if(self->plugins_, [&id](const PluginInfo& info) {
    return info.id == id;
  });
  return static_cast<std::int32_t>(commands + plugins);
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

std::int32_t PluginHost::host_register_plugin(
    void* context, const char* id, const char* title, const char* author,
    const char* version, const char* release_date, const char* description,
    const char* homepage_url, const char* icon_path, std::int32_t flags) {
  auto* self = static_cast<PluginHost*>(context);
  if (id == nullptr || *id == '\0') {
    return -1;
  }
  PluginInfo info;
  info.id = id;
  info.title = title != nullptr && *title != '\0' ? title : id;
  info.author = author != nullptr ? author : "";
  info.version = version != nullptr ? version : "";
  info.release_date = release_date != nullptr ? release_date : "";
  info.description = description != nullptr ? description : "";
  info.homepage_url = homepage_url != nullptr ? homepage_url : "";
  info.icon_path = icon_path != nullptr ? icon_path : "";
  info.built_in = (flags & 1) != 0;
  for (const auto& existing : self->plugins_) {
    if (existing.id == info.id) {
      return -1;
    }
  }
  self->current_plugin_id_ = info.id;
  self->plugins_.push_back(std::move(info));
  return 0;
}

std::int32_t PluginHost::host_register_command(
    void* context, const char* id, const char* title, const char* tooltip,
    const char* page_id, const char* group_id, const char* icon_path,
    std::int32_t order, std::int32_t flags) {
  auto* self = static_cast<PluginHost*>(context);
  if (id == nullptr || *id == '\0') {
    return -1;
  }
  PluginCommand cmd;
  cmd.id = id;
  cmd.title = title != nullptr && *title != '\0' ? title : id;
  cmd.tooltip = tooltip != nullptr ? tooltip : "";
  cmd.plugin_id = self->current_plugin_id_;
  cmd.placement.page_id = page_id != nullptr ? page_id : "";
  cmd.placement.group_id = group_id != nullptr ? group_id : "";
  resolve_ribbon_placement(cmd.placement);
  cmd.placement.icon_path = icon_path != nullptr ? icon_path : "";
  cmd.placement.order = order;
  cmd.placement.checkable = (flags & 1) != 0;
  for (const auto& existing : self->registered_) {
    if (existing.id == cmd.id) {
      return -1;
    }
  }
  self->registered_.push_back(std::move(cmd));
  return 0;
}

std::int32_t PluginHost::host_begin_point_input(
    void* context, std::uint64_t request_id, std::int32_t min_points,
    std::int32_t max_points, std::int32_t flags, float work_plane_y,
    std::int32_t preview_kind, const char* preview_curve_kind, const char* filter_kind) {
  auto* self = static_cast<PluginHost*>(context);
  if (!self->begin_point_input_ || request_id == 0 || min_points < 0 ||
      (max_points > 0 && max_points < min_points)) {
    return -1;
  }
  PluginPointInputRequest request;
  request.request_id = request_id;
  request.min_points = min_points;
  request.max_points = max_points;
  request.flags = flags;
  request.work_plane_y = work_plane_y;
  request.preview_kind = preview_kind;
  request.preview_curve_kind =
      preview_curve_kind != nullptr ? preview_curve_kind : "";
  request.filter_kind = filter_kind != nullptr ? filter_kind : "";
  auto started = self->begin_point_input_(
      std::move(request),
      [self, request_id](std::vector<PluginPickPoint> points, bool cancelled) {
        std::vector<HostPickPoint> native;
        native.reserve(points.size());
        for (const PluginPickPoint& point : points) {
          native.push_back({point.position.x, point.position.y, point.position.z, 0,
                            point.entity_id});
        }
        if (auto completed =
                self->csharp_->complete_point_input(request_id, native, cancelled);
            !completed) {
          self->emit_log(kLogError, completed.error());
        }
      });
  if (!started) {
    self->emit_log(kLogError, started.error());
    return -1;
  }
  return 0;
}

std::int32_t PluginHost::host_cancel_point_input(void* context,
                                                  std::uint64_t request_id) {
  auto* self = static_cast<PluginHost*>(context);
  if (!self->cancel_point_input_) {
    return -1;
  }
  self->cancel_point_input_(request_id);
  return 0;
}

std::int32_t PluginHost::host_set_selection(void* context, const std::uint64_t* ids,
                                            std::int32_t count) {
  auto* self = static_cast<PluginHost*>(context);
  if (self->document_ == nullptr || count < 0 || (count > 0 && ids == nullptr)) {
    return -1;
  }
  self->document_->clear_selection();
  for (std::int32_t i = 0; i < count; ++i) {
    self->document_->select(ids[static_cast<std::size_t>(i)]);
  }
  if (self->selection_changed_) {
    self->selection_changed_();
  }
  return 0;
}

std::int32_t PluginHost::host_show_dialog(void* context, std::int32_t kind, std::int32_t buttons,
                                          const char* spec_utf8, char* out_utf8,
                                          std::int32_t cap) {
  auto* self = static_cast<PluginHost*>(context);
  if (!self->show_dialog_) {
    return -1;
  }
  std::string out;
  const auto status = self->show_dialog_(kind, buttons, spec_utf8 != nullptr ? spec_utf8 : "", out);
  if (status == 0 && out_utf8 != nullptr && cap > 0) {
    fill_utf8(out, out_utf8, cap);
  }
  return status;
}

}  // namespace tamias
