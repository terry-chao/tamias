#include "viewer_host.h"

#include "engine/core/log.h"
#include "engine/document/picking.h"
#include "engine/document/document_io.h"
#include "engine/io/mesh_io.h"
#include "engine/modeling/tess_worker.h"
#include "engine/render/render_scene.h"
#if defined(TAMIAS_HAS_RHI_WEBGPU)
#include "engine/render/rhi/webgpu/webgpu_backend.h"
#endif
#if defined(TAMIAS_HAS_RHI_WEBGL)
#include "engine/render/rhi/webgl/webgl_backend.h"
#endif
#include "host/command_arg_text.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
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

bool ViewerHost::new_document() {
  // 新建 = 一个空文档 + 内置示例场景。示例走命令层（不是硬编码网格），所以几何由
  // 已注册的内核求值——WASM 上是 Truck，顺便证明"新建出来就能接着造型"。
  session_->reset_document(std::make_shared<Document>(std::string("示例")));
  loaded_ = true;
  last_submitted_scene_generation_ = 0;

  auto run = [this](const char* command, const char* args) {
    auto parsed = parse_command_arg_text(args);
    if (!parsed) {
      return false;
    }
    return static_cast<bool>(session_->dispatch(command, *parsed));
  };

  // 3m × 3m 的方形柱网：四角各一根柱，顶上四根梁连成框。
  constexpr double kHalf = 1.5;
  constexpr double kColumnHeight = 3.0;
  const double xs[2] = {-kHalf, kHalf};
  const double zs[2] = {-kHalf, kHalf};
  bool any = false;
  for (double x : xs) {
    for (double z : zs) {
      char args[160];
      std::snprintf(args, sizeof(args),
                    "s:sub_type=rect;d:width=0.4;d:depth=0.4;d:height=%g;p:points=%g,0,%g",
                    kColumnHeight, x, z);
      any = run("create_column", args) || any;
    }
  }
  // 顶圈四根梁：沿 X 两根（z = ±1.5）、沿 Z 两根（x = ±1.5），各只生成一次。
  for (double z : zs) {
    char args[192];
    std::snprintf(args, sizeof(args), "d:width=0.3;d:depth=0.4;p:points=%g,%g,%g|%g,%g,%g",
                  xs[0], kColumnHeight, z, xs[1], kColumnHeight, z);
    run("create_beam", args);
  }
  for (double x : xs) {
    char args[192];
    std::snprintf(args, sizeof(args), "d:width=0.3;d:depth=0.4;p:points=%g,%g,%g|%g,%g,%g", x,
                  kColumnHeight, zs[0], x, kColumnHeight, zs[1]);
    run("create_beam", args);
  }

  if (!any) {
    // 内核没注册（比如裁剪过的构建）时，退回内置网格演示体，至少不是空画面。
    load_demo();
    return false;
  }
  session_->document().recompute_scene();
  session_->camera().frame_aabb(session_->document().bounds());
  upload_document();
  status_ = "示例";
  return true;
}

Result<void> ViewerHost::start(const char* canvas_selector) {
  if (canvas_selector != nullptr && canvas_selector[0] != '\0') {
    canvas_selector_ = canvas_selector;
  }
  RenderDeviceConfig config{};
#if defined(TAMIAS_HAS_RHI_WEBGPU)
  register_webgpu_backend();
  config.backend = GraphicsBackend::WebGPU;
#elif defined(TAMIAS_HAS_RHI_WEBGL)
  register_webgl_backend();
  config.backend = GraphicsBackend::WebGL;
#else
  return Err("no WASM RHI backend was compiled");
#endif
  config.enable_validation = false;
  config.synchronous = true;
  render_thread_ = std::make_shared<RenderThread>(config);
  if (auto r = render_thread_->start(); !r) {
    status_ = r.error();
    return r;
  }
  channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
  if (!loaded_) {
    // 首次进入直接给一个示例文档（内核没注册时会退回内置网格演示体）。
    new_document();
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
  for (const std::uint64_t id : render_thread_->take_evicted_texture_ids()) {
    if (const TextureAsset* tex = doc.texture(id)) {
      if (auto r = render_thread_->upload_texture(id, *tex); !r) {
        log_warn(r.error());
      }
    }
  }
}

Result<void> ViewerHost::load_bytes(std::string_view name, std::span<const std::uint8_t> bytes) {
  const auto ext = lower_ext(name);
  mode_ = RenderMode::Shaded;
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
  } else if (ext == ".trscn") {
    auto loaded = deserialize_render_scene(bytes);
    if (!loaded) {
      status_ = loaded.error();
      return Err(loaded.error());
    }
    const RenderScene::View view = loaded->view;
    session_->reset_document(
        std::make_shared<Document>(document_from_render_scene(std::move(*loaded))));
    auto& cam = session_->camera().camera();
    cam.set_target(view.target);
    cam.set_distance(view.view_distance);
    cam.set_yaw_pitch(view.yaw, view.pitch);
    cam.set_fovy(view.fovy);
    cam.set_znear(view.znear);
    cam.set_zfar(view.zfar);
    cam.set_orthographic(view.orthographic);
    mode_ = view.mode;
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
    status_ = "unsupported type (use .tdoc, .trscn or .obj)";
    return Err(status_);
  }
  loaded_ = true;
  last_submitted_scene_generation_ = 0;
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

std::string ViewerHost::pick_work_plane(float nx, float ny, float plane_y) const {
  if (width_ < 2 || height_ < 2) {
    return {};
  }
  const float w = static_cast<float>(width_);
  const float h = static_cast<float>(height_);
  const float px = std::clamp(nx, 0.f, 1.f) * w;
  const float py = std::clamp(ny, 0.f, 1.f) * h;
  const Ray ray = camera_ray(session_->camera().camera(), w / h, px, py, w, h);
  if (std::fabs(ray.direction.y) < 1e-6f) {
    return {};  // 视线与工作面平行
  }
  const float t = (plane_y - ray.origin.y) / ray.direction.y;
  if (t <= 0.f) {
    return {};  // 交点在相机背后
  }
  const Vec3 p = ray.origin + ray.direction * t;
  char buffer[96];
  std::snprintf(buffer, sizeof(buffer), "%.4f,%.4f,%.4f", static_cast<double>(p.x),
                static_cast<double>(p.y), static_cast<double>(p.z));
  return std::string(buffer);
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
  frame.fovy = cam.fovy();
  frame.mode = mode_;
  frame.items = session_->document().render_items();
  frame.lod_sets = session_->document().tess_cache().snapshot();
  frame.scene_generation = session_->document().scene().generation();
  frame.scene_dirty_ids = session_->document().scene().dirty_since(last_submitted_scene_generation_);
  last_submitted_scene_generation_ = frame.scene_generation;
  channel_->resize(window(), width_, height_);
  // 没有 worker 线程的构建（WASM 不带 pthread）在这里按帧推进离散队列：每帧最多跑
  // 一个任务，把 LOD 请求摊到多帧，避免一帧里同步跑完所有离散卡死主线程。
  // 桌面有 worker 线程，pump() 是 no-op。
  TessWorker::instance().pump(1);
  for (const std::uint64_t id : session_->document().apply_completed_tess_jobs()) {
    if (const MeshAsset* asset = session_->document().mesh(id);
        asset != nullptr && !asset->cpu.vertices.empty()) {
      render_thread_->request_upload_mesh(id, asset->cpu);
    }
  }
  for (const std::uint64_t id : render_thread_->take_evicted_texture_ids()) {
    if (const TextureAsset* tex = session_->document().texture(id)) {
      if (auto r = render_thread_->upload_texture(id, *tex); !r) {
        log_warn(r.error());
      }
    }
  }
  channel_->submit(std::move(frame));
  render_thread_->pump();
  for (const LodRequest& req : render_thread_->take_lod_requests()) {
    session_->document().enqueue_lod_request(req);
  }
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
