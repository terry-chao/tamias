#include "viewer_host.h"

#include "command/command_system.h"
#include "engine/core/log.h"
#include "engine/modeling/linked_kernels.h"

#include <algorithm>
#include <cstdint>
#include <span>
#include <string>

#if defined(__EMSCRIPTEN__)
#include <emscripten/bind.h>
#endif

namespace {

tamias::ViewerHost& host() {
  static tamias::ViewerHost instance;
  return instance;
}

bool start_viewer(const std::string& canvas) {
  tamias::init_logging(tamias::LogLevel::Info);
  // 注册编进 wasm 的后端（Truck）。OCCT 不在 wasm 构建里，默认后端会退到 Truck。
  tamias::register_linked_kernels();
  tamias::register_commands(tamias::command_registry());
  auto r = host().start(canvas.c_str());
  return static_cast<bool>(r);
}

bool load_file(const std::string& name, const std::string& bytes) {
  const auto* data = reinterpret_cast<const std::uint8_t*>(bytes.data());
  auto r = host().load_bytes(name, std::span(data, bytes.size()));
  return static_cast<bool>(r);
}

// 新建文档：空文档 + 内置示例场景（走命令层，几何由 wasm 里的内核求值）。
bool new_document_viewer() { return host().new_document(); }

void resize_viewer(int width, int height) {
  host().resize(static_cast<std::uint32_t>(std::max(1, width)),
                static_cast<std::uint32_t>(std::max(1, height)));
}

void pointer_down(float x, float y, int button) { host().pointer_down(x, y, button); }
void pointer_move(float x, float y) { host().pointer_move(x, y); }
void pointer_up(float x, float y, int button) { host().pointer_up(x, y, button); }
void wheel(float delta) { host().wheel(delta); }
void frame_all() { host().frame_all(); }
void render_frame() { host().render(); }
std::string status() { return host().status(); }
std::string document_name() { return host().document_name(); }

bool dispatch_command(const std::string& command, const std::string& args) {
  return host().dispatch(command, args);
}

void undo_viewer() { host().undo(); }
void redo_viewer() { host().redo(); }
bool can_undo_viewer() { return host().can_undo(); }
bool can_redo_viewer() { return host().can_redo(); }

int selection_count() {
  return static_cast<int>(host().selection().size());
}

std::uint64_t selection_id_at(int index) {
  const auto selection = host().selection();
  if (index < 0 || static_cast<std::size_t>(index) >= selection.size()) {
    return 0;
  }
  return selection[static_cast<std::size_t>(index)];
}

void clear_selection_viewer() { host().clear_selection(); }

// 建模落点：视口归一化坐标 → 工作平面（y = planeY）上的世界点。
std::string pick_work_plane(float nx, float ny, float plane_y) {
  return host().pick_work_plane(nx, ny, plane_y);
}

}  // namespace

#if defined(__EMSCRIPTEN__)
EMSCRIPTEN_BINDINGS(tamias_viewer) {
  emscripten::function("startViewer", &start_viewer);
  emscripten::function("loadFile", &load_file);
  emscripten::function("newDocument", &new_document_viewer);
  emscripten::function("resizeViewer", &resize_viewer);
  emscripten::function("pointerDown", &pointer_down);
  emscripten::function("pointerMove", &pointer_move);
  emscripten::function("pointerUp", &pointer_up);
  emscripten::function("wheel", &wheel);
  emscripten::function("frameAll", &frame_all);
  emscripten::function("renderFrame", &render_frame);
  emscripten::function("status", &status);
  emscripten::function("documentName", &document_name);
  emscripten::function("dispatch", &dispatch_command);
  emscripten::function("undo", &undo_viewer);
  emscripten::function("redo", &redo_viewer);
  emscripten::function("canUndo", &can_undo_viewer);
  emscripten::function("canRedo", &can_redo_viewer);
  emscripten::function("selectionCount", &selection_count);
  emscripten::function("selectionIdAt", &selection_id_at);
  emscripten::function("clearSelection", &clear_selection_viewer);
  emscripten::function("pickWorkPlane", &pick_work_plane);
}
#endif

int main() {
  return 0;
}
