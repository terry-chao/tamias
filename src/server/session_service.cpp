#include "server/session_service.h"

#include "command/core/command_args.h"
#include "engine/base/log.h"
#include "engine/document/document_io.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/render/scene/render_scene.h"
#include "entity/core/entity.h"
#include "host/command_arg_text.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <utility>

namespace tamias {
namespace {

// 与 wasm 宿主（src/wasm/viewer_host.cpp 的 lower_ext）同款：按扩展名分派。
// 桌面以外的两个宿主对「内容按扩展名认」这件事必须是同一个答案。
std::string lower_ext(std::string_view name) {
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos) {
    return {};
  }
  std::string ext(name.substr(dot));
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

}  // namespace

void ensure_server_initialized() {
  static std::once_flag once;
  std::call_once(once, [] {
    init_logging(LogLevel::Info);
    // 内核与命令都得注册：没注册的话 dispatch 会报 "kernel not registered" /
    // "unknown command"，而服务壳只会看到一条 500。
    register_linked_kernels();
    register_commands(command_registry());
  });
}

SessionService::SessionService(std::string document_name) {
  ensure_server_initialized();
  session_ = std::make_unique<Session>(std::make_shared<Document>(std::move(document_name)));
}

SessionService::~SessionService() = default;

Result<void> SessionService::dispatch(std::string_view command, std::string_view args_text) {
  if (command.empty()) {
    return Err("empty command id");
  }
  auto parsed = parse_command_arg_text(args_text);
  if (!parsed) {
    return Err(parsed.error());
  }
  if (auto r = session_->dispatch(command, *parsed); !r) {
    return Err(r.error());
  }
  // 命令改完几何必须重算场景，否则后续的宽读（实体表 / 包围盒）还是旧的。
  session_->document().recompute_scene();
  return {};
}

Result<void> SessionService::load_bytes(std::string_view name, std::span<const std::uint8_t> bytes) {
  const std::string ext = lower_ext(name);
  if (ext == ".tdoc") {
    auto loaded = load_document_bytes(bytes);
    if (!loaded) {
      return Err(loaded.error());
    }
    session_->reset_document(std::make_shared<Document>(std::move(loaded->document)));
  } else if (ext == ".trscn") {
    auto loaded = deserialize_render_scene(bytes);
    if (!loaded) {
      return Err(loaded.error());
    }
    session_->reset_document(
        std::make_shared<Document>(document_from_render_scene(std::move(*loaded))));
  } else if (ext == ".obj") {
    auto mesh = load_obj_bytes(std::as_bytes(bytes));
    if (!mesh) {
      return Err(mesh.error());
    }
    session_->reset_document(std::make_shared<Document>(std::string(name)));
    session_->document().add_import_mesh(std::string(name), std::move(*mesh), Mat4::identity(),
                                         {0.75f, 0.78f, 0.82f});
  } else {
    return Err("unsupported type (use .tdoc, .trscn or .obj)");
  }
  // 服务端不渲染，但相机仍要有个合理的初值：将来导出 .trscn 快照下发时要用。
  session_->camera().frame_aabb(session_->document().bounds());
  return {};
}

std::string SessionService::document_name() const {
  return session_->document().name();
}

std::vector<std::uint64_t> SessionService::entity_ids() const {
  std::vector<std::uint64_t> ids;
  const auto& all = session_->document().entities();
  ids.reserve(all.size());
  for (const auto& [id, entity] : all) {
    (void)entity;
    ids.push_back(id);
  }
  // 排序：宽读要给「确定的第 N 个」，unordered_map 的遍历顺序不是契约。
  // 与 plugin_host.cpp 的 entity_ids() 同一取舍。
  std::sort(ids.begin(), ids.end());
  return ids;
}

std::string SessionService::entity_name(std::uint64_t id) const {
  const Entity* entity = session_->document().entity(id);
  return entity != nullptr ? entity->name : std::string{};
}

std::string SessionService::entity_kind(std::uint64_t id) const {
  const Entity* entity = session_->document().entity(id);
  return entity != nullptr ? std::string(entity_kind_name(entity->kind())) : std::string{};
}

}  // namespace tamias
