#include "viewer_host.h"

#include "engine/core/log.h"
#include "engine/document/document_io.h"
#include "engine/io/mesh_io.h"
#include "engine/render/rhi/webgl/webgl_backend.h"
#include "host/command_arg_text.h"

#include <algorithm>
#include <cctype>
#include <utility>

#if defined(__EMSCRIPTEN__)
#include <emscripten/html5.h>
#endif

namespace tamias {
namespace {

std::string lower_ext(std::string_view name) {
  std::string ext;
  const auto dot = name.find_last_of('.');
  if (dot == std::string_view::npos) {
    return {};
  }
  ext.assign(name.substr(dot));
  for (char& c : ext) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return ext;
}

}  // namespace

ViewerHost::ViewerHost()
    : session_(std::make_unique<Session>(std::make_shared<Document>("Untitled"))) {}

ViewerHost::~ViewerHost() {
  channel_.reset();
  if (render_thread_) {
    render_thread_->stop();
  }
}

NativeWindowHandle ViewerHost::window() const {
  NativeWindowHandle handle{};
  handle.canvas_selector = canvas_selector_.c_str();
  return handle;
}

void ViewerHost::load_demo() {
  auto& doc = session_->document();
  doc.add_import_mesh("demo", make_demo_cube(), Mat4::identity(), {0.75f, 0.78f, 0.82f});
  session_->camera().frame_aabb(doc.bounds());
  status_ = "demo cube";
}

Result<void> ViewerHost::start(const char* canvas_selector) {
  if (canvas_selector != nullptr && canvas_selector[0] != '\0') {
    canvas_selector_ = canvas_selector;
  }
  register_webgl_backend();
  RenderDeviceConfig config{};
  config.backend = GraphicsBackend::WebGL;
  config.enable_validation = false;
  config.synchronous = true;
  render_thread_ = std::make_shared<RenderThread>(config);
  if (auto r = render_thread_->start(); !r) {
    status_ = r.error();
    return r;
  }
  channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
  if (!loaded_) {
    load_demo();
  }
  upload_document();
  status_ = "ready";
  return {};
}

void ViewerHost::upload_document() {
  if (!render_thread_) {
    return;
  }
  const auto& doc = session_->document();
  for (const auto& [id, mesh] : doc.meshes()) {
    if (auto r = render_thread_->upload_mesh(id, mesh.cpu); !r) {
      log_warn(r.error());
    }
  }
  for (const auto& [id, tex] : doc.textures()) {
    if (auto r = render_thread_->upload_texture(id, tex); !r) {
      log_warn(r.error());
    }
  }
}

Result<void> ViewerHost::load_bytes(std::string_view name, std::span<const std::uint8_t> bytes) {
  const auto ext = lower_ext(name);
  if (ext == ".tdoc") {
    auto loaded = load_document_bytes(bytes);
    if (!loaded) {
      status_ = loaded.error();
      return Err(loaded.error());
    }
    session_->reset_document(std::make_shared<Document>(std::move(loaded->document)));
    if (loaded->has_viewport) {
      auto& cam = session_->camera().camera();
      cam.set_target(loaded->viewport.target);
      cam.set_distance(loaded->viewport.distance);
      cam.set_yaw_pitch(loaded->viewport.yaw, loaded->viewport.pitch);
      cam.set_fovy(loaded->viewport.fovy);
    } else {
      session_->camera().frame_aabb(session_->document().bounds());
    }
  } else if (ext == ".obj") {
    auto mesh = load_obj_bytes(std::as_bytes(bytes));
    if (!mesh) {
      status_ = mesh.error();
      return Err(mesh.error());
    }
    session_->reset_document(std::make_shared<Document>(std::string(name)));
    session_->document().add_import_mesh(std::string(name), std::move(*mesh), Mat4::identity(),
                                         {0.75f, 0.78f, 0.82f});
    session_->camera().frame_aabb(session_->document().bounds());
  } else {
    status_ = "unsupported type (use .tdoc or .obj)";
    return Err(status_);
  }
  loaded_ = true;
  upload_document();
  status_ = std::string(name);
  return {};
}

void ViewerHost::resize(std::uint32_t width, std::uint32_t height) {
  width_ = std::max(1u, width);
  height_ = std::max(1u, height);
#if defined(__EMSCRIPTEN__)
  emscripten_set_canvas_element_size(canvas_selector_.c_str(), static_cast<int>(width_),
                                     static_cast<int>(height_));
#endif
  if (channel_) {
    channel_->resize(window(), width_, height_);
  }
}

void ViewerHost::pointer_down(float x, float y, int button) {
  last_x_ = x;
  last_y_ = y;
  // Match desktop viewport: middle orbit, right pan. MouseEvent.button: 1 = middle, 2 = right.
  orbiting_ = button == 1;
  panning_ = button == 2;
}

void ViewerHost::pointer_move(float x, float y) {
  const float dx = x - last_x_;
  const float dy = y - last_y_;
  last_x_ = x;
  last_y_ = y;
  if (panning_) {
    session_->camera().pan(-dx, dy);
  } else if (orbiting_) {
    session_->camera().orbit(-dx, dy);
  }
}

void ViewerHost::pointer_up(float, float, int) {
  orbiting_ = false;
  panning_ = false;
}

void ViewerHost::wheel(float delta_y) {
  const float factor = delta_y > 0.f ? 1.08f : 0.92f;
  session_->camera().dolly(factor);
}

void ViewerHost::frame_all() {
  session_->camera().frame_aabb(session_->document().bounds());
}

void ViewerHost::render() {
  if (!channel_ || width_ < 2 || height_ < 2) {
    return;
  }
  const auto& cam = session_->camera().camera();
  FrameSubmission frame{};
  frame.window = window();
  frame.width = width_;
  frame.height = height_;
  const float aspect = static_cast<float>(width_) / static_cast<float>(height_);
  frame.view = cam.view_matrix();
  frame.proj = cam.proj_matrix(aspect);
  frame.eye_position = cam.eye_position();
  frame.view_distance = cam.distance();
  frame.mode = RenderMode::Shaded;
  frame.items = session_->document().render_items();
  frame.scene_generation = session_->document().scene().generation();
  frame.scene_dirty_ids = session_->document().scene().dirty_since(last_submitted_scene_generation_);
  last_submitted_scene_generation_ = frame.scene_generation;
  channel_->resize(window(), width_, height_);
  channel_->submit(std::move(frame));
  render_thread_->pump();
}

std::string ViewerHost::document_name() const {
  return session_->document().name();
}

bool ViewerHost::dispatch(std::string_view command, std::string_view args_text) {
  auto parsed = parse_command_arg_text(args_text);
  if (!parsed) {
    status_ = parsed.error();
    return false;
  }
  if (auto r = session_->dispatch(command, *parsed); !r) {
    status_ = r.error();
    return false;
  }
  session_->document().recompute_scene();
  upload_document();
  status_ = std::string(command);
  return true;
}

void ViewerHost::undo() {
  if (!session_->can_undo()) {
    return;
  }
  session_->undo();
  session_->document().recompute_scene();
  upload_document();
}

void ViewerHost::redo() {
  if (!session_->can_redo()) {
    return;
  }
  session_->redo();
  session_->document().recompute_scene();
  upload_document();
}

}  // namespace tamias
