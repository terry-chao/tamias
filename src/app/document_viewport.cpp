#include "document_viewport.h"

#include "app_settings.h"
#include "bim/host_geometry.h"
#include "bim/wall_size.h"
#include "command/edit_entity_grip_command.h"
#include "command/import_texture_command.h"
#include "command/replace_texture_command.h"
#include "command/update_material_command.h"
#include "component_specs.h"
#include "engine/core/log.h"
#include "engine/math/grid.h"
#include "engine/modeling/curve_geom.h"
#include "engine/modeling/feature.h"
#include "engine/profile/timing_scope.h"
#include "entity/entity.h"
#include "entity/entity_grip.h"

#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QCursor>
#include <QIcon>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <QGuiApplication>
#include <qpa/qplatformnativeinterface.h>
#endif

namespace tamias {

class DocumentViewport::NativeSurface final : public QWidget {
 public:
  explicit NativeSurface(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_PaintOnScreen);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
  }

  QPaintEngine* paintEngine() const override { return nullptr; }

 protected:
  // Native HWND children eat Win32 mouse messages; forward them to the viewport
  // so orbit / pan / pick keep working over the Vulkan surface.
  bool event(QEvent* e) override {
    switch (e->type()) {
      case QEvent::MouseButtonPress:
      case QEvent::MouseButtonRelease:
      case QEvent::MouseButtonDblClick:
      case QEvent::MouseMove:
      case QEvent::Wheel:
        if (parentWidget()) {
          return QCoreApplication::sendEvent(parentWidget(), e);
        }
        break;
      default:
        break;
    }
    return QWidget::event(e);
  }
};

DocumentViewport::DocumentViewport(std::shared_ptr<Document> document,
                                   std::shared_ptr<RenderThread> render_thread, QWidget* parent)
    : QWidget(parent),
      session_(std::make_unique<Session>(std::move(document))),
      document_(&session_->document()),
      command_system_(session_->command_system()),
      camera_(session_->camera().camera()),
      render_thread_(std::move(render_thread)) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Match Vulkan clear so any uncovered edge never flashes pure black.
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(36, 46, 61));
  setPalette(pal);

  // Vulkan draws into a native child surface; this parent stays a normal Qt
  // widget so overlays (view cube) can paint and receive clicks on top.
  // Keep the surface in a layout so it tracks the viewport size from the first
  // show — manual setGeometry alone often leaves a tiny HWND at (0,0) until the
  // user resizes/interacts.
  surface_ = new NativeSurface(this);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(surface_);

  view_cube_ = new ViewCubeWidget(this);
  connect(view_cube_, &ViewCubeWidget::face_clicked, this, &DocumentViewport::on_view_cube_face);
  connect(view_cube_, &ViewCubeWidget::corner_clicked, this, &DocumentViewport::on_view_cube_corner);
  connect(view_cube_, &ViewCubeWidget::orbit_dragged, this, [this](float dyaw, float dpitch) {
    stop_view_animation();
    if (plan_view_) {
      set_plan_view(false, false);
    }
    camera_.orbit(dyaw, dpitch);
    request_redraw();
  });

  tool_strip_ = new ViewportToolStrip(this);
  connect(tool_strip_, &ViewportToolStrip::plan_view_toggled, this,
          [this](bool plan) { set_plan_view(plan); });
  connect(tool_strip_, &ViewportToolStrip::frame_all_clicked, this, &DocumentViewport::frame_scene);
  connect(tool_strip_, &ViewportToolStrip::visibility_menu_about_to_show, this,
          &DocumentViewport::populate_visibility_menu);
  connect(tool_strip_, &ViewportToolStrip::floor_menu_about_to_show, this,
          &DocumentViewport::populate_floor_menu);

  view_anim_timer_ = new QTimer(this);
  view_anim_timer_->setInterval(16);
  connect(view_anim_timer_, &QTimer::timeout, this, &DocumentViewport::on_view_anim_tick);

  coord_label_ = new QLabel(this);
  coord_label_->setObjectName(QStringLiteral("coordReadout"));
  coord_label_->setAttribute(Qt::WA_NativeWindow);
  coord_label_->setAttribute(Qt::WA_TransparentForMouseEvents);
  coord_label_->setStyleSheet(QStringLiteral(
      "QLabel#coordReadout {"
      "  background: rgba(20, 22, 26, 180);"
      "  color: #d8d4ce;"
      "  border: 1px solid #3a3c42;"
      "  border-radius: 6px;"
      "  padding: 6px 10px;"
      "  font-family: Consolas, 'Cascadia Mono', monospace;"
      "  font-size: 12px;"
      "}"));

  box_select_overlay_ = new BoxSelectOverlay(this);

  camera_.frame_aabb(document_->bounds().valid() ? document_->bounds()
                                                 : Aabb{{-1, -1, -1}, {1, 1, 1}});
  rebuild_bvh();
  sync_view_cube();
  sync_coord_readout();
}

DocumentViewport::~DocumentViewport() {
  cancel_plugin_point_input();
  // Tear down the swapchain/surface while the native HWND is still valid, and
  // wait for the render thread so it cannot draw into a destroyed window.
  alive_ = false;
  channel_.reset();
  render_thread_.reset();
  destroy_gl_surface();
}

void DocumentViewport::set_render_mode(RenderMode mode) {
  mode_ = mode;
  request_redraw();
}

ViewportState DocumentViewport::capture_viewport_state() const {
  ViewportState state;
  state.target = camera_.target();
  state.distance = camera_.distance();
  state.yaw = camera_.yaw();
  state.pitch = camera_.pitch();
  state.fovy = camera_.fovy();
  state.znear = camera_.znear();
  state.zfar = camera_.zfar();
  state.render_mode = static_cast<ViewRenderMode>(mode_);
  return state;
}

RenderScene::View DocumentViewport::capture_render_scene_view() const {
  RenderScene::View view;
  const qreal dpr = devicePixelRatioF();
  const int sw = surface_ != nullptr ? surface_->width() : std::max(1, width());
  const int sh = surface_ != nullptr ? surface_->height() : std::max(1, height());
  view.width = static_cast<std::uint32_t>(std::max(1, static_cast<int>(sw * dpr)));
  view.height = static_cast<std::uint32_t>(std::max(1, static_cast<int>(sh * dpr)));
  const float aspect =
      static_cast<float>(view.width) / static_cast<float>(std::max(1u, view.height));
  view.view = camera_.view_matrix();
  view.proj = camera_.proj_matrix(aspect);
  view.eye_position = camera_.eye_position();
  view.target = camera_.target();
  view.view_distance = camera_.distance();
  view.yaw = camera_.yaw();
  view.pitch = camera_.pitch();
  view.fovy = camera_.fovy();
  view.znear = camera_.znear();
  view.zfar = camera_.zfar();
  view.mode = mode_;
  view.orthographic = camera_.orthographic();
  return view;
}

std::vector<std::uint64_t> DocumentViewport::capture_hidden_node_ids() const {
  std::vector<std::uint64_t> hidden;
  for (const auto& item : document_->render_items()) {
    if (!node_visible_in_view(item.node_id)) {
      hidden.push_back(item.node_id);
    }
  }
  std::sort(hidden.begin(), hidden.end());
  hidden.erase(std::unique(hidden.begin(), hidden.end()), hidden.end());
  return hidden;
}

RenderScene DocumentViewport::capture_debug_scene() const {
  RenderScene scene = document_->capture_render_scene(capture_render_scene_view());
  scene.hidden_node_ids = capture_hidden_node_ids();
  if (render_thread_ != nullptr) {
    scene.debug_graph.lod_by_node = render_thread_->last_lod_by_node();
  }
  return scene;
}

void DocumentViewport::apply_viewport_state(const ViewportState& state) {
  stop_view_animation();
  camera_.set_target(state.target);
  camera_.set_distance(state.distance);
  camera_.set_yaw_pitch(state.yaw, state.pitch);
  camera_.set_fovy(state.fovy);
  mode_ = static_cast<RenderMode>(state.render_mode);
  sync_view_cube();
  // Do not submit here if the widget is not yet laid out — an early present at the
  // wrong size can leave the swapchain stuck drawing into a corner of the window.
  if (isVisible() && surface_ && surface_->width() >= 2 && surface_->height() >= 2) {
    request_redraw();
  }
}

void DocumentViewport::frame_scene() {
  stop_view_animation();
  camera_.frame_aabb(document_->bounds());
  request_redraw();
}

void DocumentViewport::frame_node(std::uint64_t node_id) {
  if (document_ == nullptr || node_id == 0) {
    return;
  }
  const SceneNode* node = document_->scene().find(node_id);
  if (node == nullptr || !node->world_bounds.valid()) {
    return;
  }
  stop_view_animation();
  camera_.frame_aabb(node->world_bounds);
  request_redraw();
}

void DocumentViewport::request_redraw() {
  if (!alive_) {
    return;
  }
  sync_view_cube();
  sync_coord_readout();
  submit_current_frame();
}

void DocumentViewport::layout_overlays() {
  if (surface_) {
    surface_->lower();
    // Force HWND creation after layout so the first Vulkan present sees the
    // real client extent, not a default 0x0 / stub size.
    if (width() > 1 && height() > 1) {
      (void)surface_->winId();
    }
  }
  constexpr int kMargin = 12;
  if (view_cube_) {
    view_cube_->move(width() - view_cube_->width() - kMargin, kMargin);
    view_cube_->raise();
  }
  if (tool_strip_) {
    const int strip_y = view_cube_ ? (kMargin + view_cube_->height() + 8) : kMargin;
    tool_strip_->move(width() - tool_strip_->width() - kMargin, strip_y);
    tool_strip_->raise();
  }
  if (coord_label_) {
    coord_label_->adjustSize();
    coord_label_->move(kMargin, height() - coord_label_->height() - kMargin);
    coord_label_->raise();
  }
  if (box_select_overlay_ && box_select_overlay_->isVisible()) {
    box_select_overlay_->raise();
  }
}

void DocumentViewport::sync_view_cube() {
  if (view_cube_) {
    view_cube_->set_orientation(camera_.yaw(), camera_.pitch());
  }
}

Vec3 DocumentViewport::cursor_world_position(const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));

  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        return node_visible_in_view(id);
      })) {
    return ray.origin + ray.direction * hit->t;
  }

  // Fall back to the horizontal plane through the camera target.
  const float plane_y = camera_.target().y;
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      return ray.origin + ray.direction * t;
    }
  }
  return ray.origin + ray.direction * camera_.distance();
}

void DocumentViewport::sync_coord_readout() {
  if (!coord_label_) {
    return;
  }
  QString text;
  if (!has_cursor_) {
    const Vec3 target = camera_.target();
    text = tr("X %1  Y %2  Z %3")
               .arg(target.x, 0, 'f', 3)
               .arg(target.y, 0, 'f', 3)
               .arg(target.z, 0, 'f', 3);
  } else {
    const Vec3 p = grid_snap_active() ? cursor_ground_position(last_mouse_)
                                      : cursor_world_position(last_mouse_);
    text = tr("X %1  Y %2  Z %3")
               .arg(p.x, 0, 'f', 3)
               .arg(p.y, 0, 'f', 3)
               .arg(p.z, 0, 'f', 3);
  }
  if (render_thread_) {
    const RenderFrameStats stats = render_thread_->last_stats();
    text += tr("\ndraw %1  tri %2  gpu %3MB  tess %4")
                .arg(stats.draws)
                .arg(stats.triangles)
                .arg(static_cast<double>(stats.gpu_mesh_bytes) / (1024.0 * 1024.0), 0, 'f', 1)
                .arg(document_->pending_tessellate_count() + stats.lod_requests);
  }
  coord_label_->setText(text);
  layout_overlays();
}

void DocumentViewport::stop_view_animation() {
  if (view_anim_timer_ && view_anim_timer_->isActive()) {
    view_anim_timer_->stop();
  }
}

void DocumentViewport::start_view_animation(float target_yaw, float target_pitch,
                                            bool finish_orthographic) {
  constexpr float kPi = 3.141592654f;
  anim_finish_orthographic_ = finish_orthographic;
  anim_from_yaw_ = camera_.yaw();
  anim_from_pitch_ = camera_.pitch();
  anim_to_yaw_ = target_yaw;
  anim_to_pitch_ = std::clamp(target_pitch, -kHalfPi, kHalfPi);

  // Shortest yaw arc so e.g. Front↔Back never spins the long way.
  float delta = anim_to_yaw_ - anim_from_yaw_;
  while (delta > kPi) {
    delta -= 2.f * kPi;
  }
  while (delta < -kPi) {
    delta += 2.f * kPi;
  }
  anim_yaw_delta_ = delta;

  if (std::abs(anim_yaw_delta_) < 1e-4f && std::abs(anim_to_pitch_ - anim_from_pitch_) < 1e-4f) {
    camera_.set_yaw_pitch(anim_to_yaw_, anim_to_pitch_);
    if (anim_finish_orthographic_) {
      camera_.set_orthographic(true);
      anim_finish_orthographic_ = false;
    }
    stop_view_animation();
    request_redraw();
    return;
  }

  view_anim_clock_.restart();
  view_anim_timer_->start();
  this->on_view_anim_tick();
}

void DocumentViewport::on_view_anim_tick() {
  constexpr int kDurationMs = 280;
  const float t_raw =
      std::clamp(static_cast<float>(view_anim_clock_.elapsed()) / static_cast<float>(kDurationMs),
                 0.f, 1.f);
  // Ease-out cubic.
  const float t = 1.f - (1.f - t_raw) * (1.f - t_raw) * (1.f - t_raw);
  const float yaw = anim_from_yaw_ + anim_yaw_delta_ * t;
  const float pitch = anim_from_pitch_ + (anim_to_pitch_ - anim_from_pitch_) * t;
  camera_.set_yaw_pitch(yaw, pitch);
  request_redraw();

  if (t_raw >= 1.f) {
    camera_.set_yaw_pitch(anim_to_yaw_, anim_to_pitch_);
    if (anim_finish_orthographic_) {
      camera_.set_orthographic(true);
      anim_finish_orthographic_ = false;
    }
    stop_view_animation();
    request_redraw();
  }
}

void DocumentViewport::on_view_cube_face(ViewCubeFace face) {
  float yaw = camera_.yaw();
  float pitch = 0.f;
  switch (face) {
    case ViewCubeFace::Front:
      yaw = 0.f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Back:
      yaw = 3.141592654f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Left:
      yaw = -1.570796327f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Right:
      yaw = 1.570796327f;
      pitch = 0.f;
      break;
    case ViewCubeFace::Top:
      pitch = kHalfPi;
      break;
    case ViewCubeFace::Bottom:
      pitch = -kHalfPi;
      break;
  }
  if (plan_view_ && face != ViewCubeFace::Top) {
    set_plan_view(false, false);
  }
  start_view_animation(yaw, pitch);
}

void DocumentViewport::on_view_cube_corner(ViewCubeCorner corner) {
  // Eye along the chosen cube corner (Front=+Z, Right=+X, Top=+Y).
  Vec3 dir{};
  switch (corner) {
    case ViewCubeCorner::RightTopFront:
      dir = {1.f, 1.f, 1.f};
      break;
    case ViewCubeCorner::LeftTopFront:
      dir = {-1.f, 1.f, 1.f};
      break;
    case ViewCubeCorner::RightTopBack:
      dir = {1.f, 1.f, -1.f};
      break;
    case ViewCubeCorner::LeftTopBack:
      dir = {-1.f, 1.f, -1.f};
      break;
    case ViewCubeCorner::RightBottomFront:
      dir = {1.f, -1.f, 1.f};
      break;
    case ViewCubeCorner::LeftBottomFront:
      dir = {-1.f, -1.f, 1.f};
      break;
    case ViewCubeCorner::RightBottomBack:
      dir = {1.f, -1.f, -1.f};
      break;
    case ViewCubeCorner::LeftBottomBack:
      dir = {-1.f, -1.f, -1.f};
      break;
  }
  const Vec3 d = normalize(dir);
  const float pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
  const float yaw = std::atan2(d.x, d.z);
  if (plan_view_) {
    set_plan_view(false, false);
  }
  start_view_animation(yaw, pitch);
}

NativeWindowHandle DocumentViewport::native_handle() const {
  NativeWindowHandle handle{};
#if defined(_WIN32)
  if (gl_hwnd_) {
    handle.hwnd = gl_hwnd_;
  } else {
    handle.hwnd = reinterpret_cast<void*>(surface_ ? surface_->winId() : winId());
  }
#else
  if (auto* ni = QGuiApplication::platformNativeInterface()) {
    handle.display = ni->nativeResourceForIntegration("display");
  }
  handle.window = static_cast<std::uint64_t>(surface_ ? surface_->winId() : winId());
#endif
  return handle;
}

void DocumentViewport::destroy_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_) {
    destroy_opengl_surface_hwnd(gl_hwnd_);
    gl_hwnd_ = nullptr;
  }
#endif
}

void DocumentViewport::ensure_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_ || !surface_) {
    return;
  }
  if (AppSettings::instance().graphics_backend() != GraphicsBackend::OpenGL) {
    return;
  }
  // Force the Qt native child to exist, then create our GL HWND on the UI thread.
  const WId parent_id = surface_->winId();
  if (!parent_id) {
    return;
  }
  const auto dpr = devicePixelRatioF();
  const int w = std::max(1, static_cast<int>(surface_->width() * dpr));
  const int h = std::max(1, static_cast<int>(surface_->height() * dpr));
  gl_hwnd_ = create_opengl_surface_hwnd(reinterpret_cast<void*>(parent_id), w, h);
  if (!gl_hwnd_) {
    log_error("Failed to create OpenGL surface HWND on UI thread");
  }
#endif
}

void DocumentViewport::ensure_channel() {
  if (!alive_ || channel_) {
    return;
  }
  if (!render_thread_) {
    const RenderDeviceConfig config = AppSettings::instance().render_device_config();
    render_thread_ = RenderThreadPool::instance().acquire(config);
  }
  if (!render_thread_) {
    log_error("failed to acquire RenderThread");
    return;
  }
  channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
}

void DocumentViewport::rebuild_bvh() { bvh_.build(*document_); }

void DocumentViewport::submit_current_frame() {
  TimingScope scope("submit_current_frame", TimingCategory::Render);
  if (!alive_) {
    return;
  }
  ensure_channel();
  if (!channel_ || !surface_) {
    return;
  }
  // Skip until the surface has a real laid-out size; a 1x1 present on first
  // show leaves a black window until the next resize.
  if (surface_->width() < 2 || surface_->height() < 2) {
    return;
  }
  if (render_thread_) {
    for (const std::uint64_t id : document_->apply_completed_tess_jobs()) {
      if (const MeshAsset* asset = document_->mesh(id);
          asset != nullptr && !asset->cpu.vertices.empty()) {
        render_thread_->request_upload_mesh(id, asset->cpu);
      }
    }
    for (const LodRequest& req : render_thread_->take_lod_requests()) {
      document_->enqueue_lod_request(req);
    }
  }
  ensure_gl_surface();
  const auto dpr = devicePixelRatioF();
  const auto w = static_cast<std::uint32_t>(surface_->width() * dpr);
  const auto h = static_cast<std::uint32_t>(surface_->height() * dpr);
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_) {
    resize_opengl_surface_hwnd(gl_hwnd_, static_cast<int>(w), static_cast<int>(h));
  }
#endif
  channel_->resize(native_handle(), w, h);
  resync_textures();

  FrameSubmission frame{};
  frame.window = native_handle();
  frame.width = w;
  frame.height = h;
  const float aspect = static_cast<float>(w) / static_cast<float>(h);
  frame.view = camera_.view_matrix();
  frame.proj = camera_.proj_matrix(aspect);
  frame.eye_position = camera_.eye_position();
  frame.view_distance = camera_.distance();
  frame.fovy = camera_.fovy();
  frame.mode = mode_;
  refresh_floors();
  // 留存场景图的同步源必须是全量清单（无视锥剔除）；剔除/可见性过滤移到渲染
  // 线程录制时按节点判断，树本身保持完整。
  frame.items = document_->render_items();
  frame.lod_sets = document_->tess_cache().snapshot();
  std::vector<std::uint64_t> hidden;
  hidden.reserve(frame.items.size());
  for (const auto& item : frame.items) {
    if (!node_visible_in_view(item.node_id)) {
      hidden.push_back(item.node_id);
    }
  }
  frame.hidden_node_ids = std::move(hidden);
  if (debug_isolate_node_) {
    for (const auto& item : frame.items) {
      if (item.node_id != *debug_isolate_node_) {
        frame.hidden_node_ids.push_back(item.node_id);
      }
    }
  }
  frame.scene_generation = document_->scene().generation();
  frame.scene_dirty_ids = document_->scene().dirty_since(last_submitted_scene_generation_);
  last_submitted_scene_generation_ = frame.scene_generation;
  const Vec3 cursor = has_cursor_ ? cursor_ground_position(last_mouse_) : Vec3{};
  if (plugin_point_input_.active()) {
    std::vector<Vec3> controls;
    controls.reserve(plugin_point_input_.points().size() + 1);
    for (const PluginPickPoint& point : plugin_point_input_.points()) {
      controls.push_back(point.position);
    }
    if (has_cursor_ &&
        (controls.empty() || length(controls.back() - cursor) >= 1e-4f)) {
      controls.push_back(cursor);
    }
    frame.preview_control_polyline = controls;
    frame.preview_points = controls;
    if (controls.size() >= 2 && plugin_point_input_.preview_kind() != 0) {
      const int kind = plugin_point_input_.preview_kind();
      const std::string& curve = plugin_point_input_.preview_curve_kind();
      if (kind == 1) {
        if (curve == "nurbs") {
          frame.preview_polyline = sample_nurbs(controls, {});
        } else if (curve == "bspline") {
          frame.preview_polyline = sample_bspline(controls);
        } else if (curve == "bezier") {
          frame.preview_polyline = sample_bezier(controls);
        } else {
          frame.preview_polyline = controls;
        }
      } else if (kind == 2 || kind == 7) {
        frame.preview_polyline = {controls.front(), controls.back()};
      } else if (kind == 3) {
        frame.preview_polyline = controls;
      } else if (kind == 4 || kind == 8) {
        frame.preview_polyline = sample_rect_xz(controls.front(), controls.back());
      } else if (kind == 5) {
        frame.preview_polyline =
            sample_circle_xz(controls.front(), length(controls.back() - controls.front()));
      } else if (kind == 6) {
        if (controls.size() >= 3) {
          frame.preview_polyline = sample_arc_3pt(controls[0], controls[1], controls.back());
        } else {
          frame.preview_polyline = {controls.front(), controls.back()};
        }
      } else {
        frame.preview_polyline = controls;
      }
    }
  } else {
    if (is_opening_placement_tool() && command_system_.has_pending() && has_cursor_) {
      update_opening_hover(last_mouse_);
    }
    frame.preview_polyline = command_system_.preview_polyline(cursor);
    frame.preview_control_polyline = command_system_.preview_control_polyline(cursor);
    frame.preview_points = command_system_.preview_points(cursor);
  }
  if (grid_snap_active() && has_cursor_ && is_on_grid_xz(cursor)) {
    frame.snap_point = cursor;
  }
  fill_grip_overlay(frame);
  fill_debug_overlay(frame);
  channel_->submit(std::move(frame));
}

void DocumentViewport::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  layout_overlays();
  ensure_channel();
  request_redraw();
  // Qt finishes applying the native child size after showEvent returns; present
  // once more on the next event-loop tick so the first frame matches the HWND.
  QTimer::singleShot(0, this, [this] {
    if (!alive_) {
      return;
    }
    layout_overlays();
    request_redraw();
  });
}

void DocumentViewport::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layout_overlays();
  request_redraw();
}

void DocumentViewport::mousePressEvent(QMouseEvent* event) {
  setFocus();  // 让视口能收到按键（[ ] 改选中对象的参数）
  last_mouse_ = event->pos();
  press_mouse_ = event->pos();
  has_cursor_ = true;
  press_hit_ = 0;
  if (event->button() == Qt::LeftButton) {
    if (plugin_point_input_.active()) {
      plugin_input_press_ = true;
      Vec3 point = cursor_ground_position(event->pos());
      std::uint64_t picked = 0;
      if (plugin_point_input_.pick_entities()) {
        const auto dpr = devicePixelRatioF();
        const float aspect = static_cast<float>((std::max)(1, width())) /
                             static_cast<float>((std::max)(1, height()));
        const Ray ray =
            camera_ray(camera_, aspect, static_cast<float>(event->pos().x() * dpr),
                       static_cast<float>(event->pos().y() * dpr),
                       static_cast<float>(width() * dpr),
                       static_cast<float>(height() * dpr));
        if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
              return node_visible_in_view(id);
            })) {
          picked = hit->node_id;
          point = ray.origin + ray.direction * hit->t;
        }
      }
      if (plugin_point_input_.entities_only()) {
        if (picked == 0) {
          return;
        }
        const std::string& filter = plugin_point_input_.filter_kind();
        if (!filter.empty()) {
          const Entity* entity = document_->entity(picked);
          if (entity == nullptr || entity_kind_name(entity->kind()) != filter) {
            return;
          }
        }
      }
      plugin_point_input_.add_point({point, picked});
      request_redraw();
      return;
    }
    if (command_system_.has_pending()) {
      if (session_->tool_mode() == ToolMode::Slab && !plan_view_) {
        refuse_slab_outside_plan(true);
        set_tool(ToolMode::None);
        return;
      }
      plugin_input_press_ = session_->tool_mode() == ToolMode::None;
      Vec3 point = cursor_ground_position(event->pos());
      std::uint64_t picked = 0;
      if (is_opening_placement_tool()) {
        if (auto wall = pick_wall_at(event->pos())) {
          picked = wall->first;
          point = wall->second;
        } else {
          emit status_message(session_->tool_mode() == ToolMode::Door
                                  ? tr("Click a wall to place the door")
                                  : tr("Click a wall to place the window"));
          request_redraw();
          return;
        }
      }
      auto r = command_system_.feed_point(point, picked);  // 喂交互点给 pending 命令
      if (finish_pending_if_done(r)) {
        return;
      }
      request_redraw();
      return;
    }
    press_hit_ = pick_node_at(event->pos());
    EntityGrip grip{};
    if (pick_grip_at(event->pos(), grip)) {
      Entity* entity = document_->entity(grip.entity_id);
      if (entity != nullptr) {
        gripping_ = true;
        active_grip_ = grip;
        grip_from_model_ = entity->model;
        grip_from_transform_ = entity->local_transform;
        grip_from_world_ = grip.world;
        clear_grip_preview();
        return;
      }
    }
    const bool shift = (event->modifiers() & Qt::ShiftModifier) != 0;
    if (press_hit_ != 0) {
      const SceneNode* node = document_->scene().find(press_hit_);
      const bool already = node != nullptr && node->selected;
      if (shift) {
        if (already) {
          session_->deselect(press_hit_);
        } else {
          session_->select(press_hit_);
        }
      } else if (!already) {
        session_->clear_selection();
        session_->select(press_hit_);
      }
      emit selection_changed();
      request_redraw();
    }
    return;
  }
  if (event->button() == Qt::MiddleButton) {
    stop_view_animation();
    mmb_nav_ = true;
    return;
  }
  if (event->button() == Qt::RightButton) {
    stop_view_animation();
    panning_ = true;
  }
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPoint delta = event->pos() - last_mouse_;
  last_mouse_ = event->pos();
  has_cursor_ = true;
  if (mmb_nav_ && (event->buttons() & Qt::MiddleButton)) {
    if (plan_view_ || (event->modifiers() & Qt::ShiftModifier)) {
      session_->camera().pan(-delta.x(), delta.y());
    } else {
      session_->camera().orbit(-delta.x(), delta.y());
    }
    request_redraw();
    return;
  }
  if (panning_) {
    session_->camera().pan(-delta.x(), delta.y());
    request_redraw();
    return;
  }
  if (gripping_ && (event->buttons() & Qt::LeftButton)) {
    apply_grip_at(event->pos());
    return;
  }
  if (plugin_point_input_.active() || command_system_.has_pending()) {
    update_opening_hover(event->pos());
    request_redraw();  // 更新网格捕捉点与预览线
    return;
  }
  if (event->buttons() & Qt::LeftButton) {
    if (!box_selecting_ && press_hit_ == 0 &&
        (event->pos() - press_mouse_).manhattanLength() >= 4) {
      box_selecting_ = true;
    }
    if (box_selecting_) {
      update_box_select_rect(event->pos());
      return;
    }
  }
  EntityGrip hover{};
  if (pick_grip_at(event->pos(), hover)) {
    setCursor(Qt::SizeAllCursor);
  } else {
    unsetCursor();
  }
  sync_coord_readout();
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    if (plugin_input_press_) {
      plugin_input_press_ = false;
    } else if (gripping_) {
      commit_grip_drag();
      gripping_ = false;
    } else if (session_->tool_mode() == ToolMode::None) {
      if (box_selecting_) {
        finish_box_select(event->pos(), (event->modifiers() & Qt::ShiftModifier) != 0);
      } else if ((event->pos() - press_mouse_).manhattanLength() < 4 && press_hit_ == 0 &&
                 (event->modifiers() & Qt::ShiftModifier) == 0) {
        session_->clear_selection();
        request_redraw();
        emit selection_changed();
      }
    }
    box_selecting_ = false;
    if (box_select_overlay_) {
      box_select_overlay_->hide_box();
    }
  }
  if (event->button() == Qt::MiddleButton) {
    mmb_nav_ = false;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = false;
    if ((event->pos() - press_mouse_).manhattanLength() < 4) {
      if (plugin_point_input_.active()) {
        plugin_point_input_.cancel();
        request_redraw();
        return;
      }
      if (session_->tool_mode() == ToolMode::None &&
          command_system_.has_pending()) {
        if (command_system_.accepts_confirm()) {
          if (!finish_pending_if_done(command_system_.confirm())) {
            command_system_.cancel();
            request_redraw();
          }
        } else {
          command_system_.cancel();
          request_redraw();
        }
        return;
      }
      if (session_->tool_mode() != ToolMode::None) {
        // 折线 / 贝塞尔：右键先提交已点的顶点，再退出工具。
        if (command_system_.accepts_confirm()) {
          const Result<bool> done = command_system_.confirm();
          if (done && *done) {
            resync_all_meshes();
            rebuild_bvh();
            emit document_changed();
          }
        }
        set_tool(ToolMode::None);
        return;
      }
      if (const std::uint64_t hit = pick_node_at(event->pos());
          hit != 0 && document_->entity(hit) != nullptr) {
        session_->clear_selection();
        session_->select(hit);
        request_redraw();
        emit selection_changed();
      }
      if (document_->selected_entity() != nullptr) {
        show_entity_context_menu(mapToGlobal(event->pos()));
      }
    }
  }
}

void DocumentViewport::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton && plugin_point_input_.active() &&
      plugin_point_input_.accepts_confirm()) {
    plugin_point_input_.confirm();
    request_redraw();
    return;
  }
  if (event->button() != Qt::LeftButton || !command_system_.accepts_confirm()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  last_mouse_ = event->pos();
  (void)finish_pending_if_done(command_system_.confirm());
}

void DocumentViewport::wheelEvent(QWheelEvent* event) {
  stop_view_animation();
  const float steps = event->angleDelta().y() / 120.f;
  const float factor = std::pow(0.9f, steps);
  const QPoint pos = event->position().toPoint();
  last_mouse_ = pos;
  has_cursor_ = true;

  if (AppSettings::instance().zoom_to_mouse_position()) {
    // Zoom toward the cursor: sample the world point first, dolly, then shift
    // the orbit target so that point stays fixed in screen space.
    const Vec3 focus = cursor_world_position(pos);
    session_->camera().dolly_to_focus(factor, focus);
  } else {
    session_->camera().dolly(factor);
  }
  request_redraw();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event) {
  if (plugin_point_input_.active()) {
    if (event->key() == Qt::Key_Escape) {
      plugin_point_input_.cancel();
      request_redraw();
      return;
    }
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) &&
        plugin_point_input_.accepts_confirm()) {
      plugin_point_input_.confirm();
      request_redraw();
      return;
    }
  }
  switch (event->key()) {
    case Qt::Key_Escape:
      cancel_tool();
      break;
    case Qt::Key_Delete:
      if (session_->tool_mode() == ToolMode::None &&
          !command_system_.has_pending()) {
        delete_selected();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (command_system_.accepts_confirm()) {
        (void)finish_pending_if_done(command_system_.confirm());
      }
      break;
    case Qt::Key_BracketLeft:   // '[' 减参数
      adjust_selected_param(-0.05);
      break;
    case Qt::Key_BracketRight:  // ']' 加参数
      adjust_selected_param(0.05);
      break;
    default:
      QWidget::keyPressEvent(event);
      break;
  }
}

void DocumentViewport::refuse_slab_outside_plan(bool popup) {
  const QString text = tr("Slabs can only be drawn in plan view");
  emit status_message(text);
  if (popup) {
    QMessageBox::warning(this, tr("Slab"), text);
  }
}

void DocumentViewport::set_tool(ToolMode mode) {
  unsetCursor();
  session_->set_tool(mode);
  cancel_plugin_point_input();
  command_system_.cancel();
  if (mode != ToolMode::None) {
    setFocus();
    // 有绘制规格的构件（墙/梁/柱/板/门/窗/结构墙/基础/幕墙）走面板武装，
    // 点 icon 不立即绘制；其余（草图）沿用立即 dispatch。
    if (find_component_spec(mode) == nullptr) {
      dispatch_tool_command(mode);
    }
  }
  request_redraw();
  emit tool_mode_changed(mode);
}

void DocumentViewport::arm_create(ToolMode mode, const CommandArgs& args) {
  if (mode == ToolMode::Slab && !plan_view_) {
    refuse_slab_outside_plan(true);
    emit tool_mode_changed(session_->tool_mode());
    return;
  }
  session_->set_tool(mode);
  cancel_plugin_point_input();
  command_system_.cancel();
  setFocus();
  const ComponentSpec* spec = find_component_spec(mode);
  if (spec == nullptr) {
    log_error("arm_create: no component spec for tool");
    return;
  }
  if (auto r = session_->dispatch(spec->command.toStdString(), args); !r) {
    log_error(r.error());
  }
  last_arm_mode_ = mode;
  last_arm_args_ = args;
  request_redraw();
  emit tool_mode_changed(mode);
}

void DocumentViewport::rearm_tool() {
  const ToolMode mode = session_->tool_mode();
  if (mode == ToolMode::None) {
    return;
  }
  if (find_component_spec(mode) != nullptr && last_arm_mode_ == mode) {
    // 面板构件：用上次武装参数重新 dispatch。
    command_system_.cancel();
    const ComponentSpec* spec = find_component_spec(mode);
    if (auto r = session_->dispatch(spec->command.toStdString(), last_arm_args_); !r) {
      log_error(r.error());
    }
  } else {
    dispatch_tool_command(mode);
  }
}

void DocumentViewport::dispatch_tool_command(ToolMode mode) {
  if (mode == ToolMode::Wall) {
    if (auto r = session_->dispatch("create_wall",
                                          {{"thickness", kDefaultWallThickness},
                                           {"height", kDefaultWallHeight}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Beam) {
    if (auto r = session_->dispatch("create_beam",
                                          {{"width", 0.3}, {"depth", 0.5}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Column) {
    if (auto r = session_->dispatch("create_column", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Slab) {
    if (auto r = session_->dispatch(
            "create_slab",
            {{"thickness", 0.2}, {"elevation", kDefaultWallHeight}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Door) {
    if (auto r = session_->dispatch("create_door", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Window) {
    if (auto r = session_->dispatch("create_window", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Line) {
    if (auto r = session_->dispatch("create_line", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Polyline) {
    if (auto r = session_->dispatch("create_polyline", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Circle) {
    if (auto r = session_->dispatch("create_circle", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Arc) {
    if (auto r = session_->dispatch("create_arc", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Bezier) {
    if (auto r = session_->dispatch("create_bezier", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::BSpline) {
    if (auto r = session_->dispatch("create_bspline", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Rectangle) {
    if (auto r = session_->dispatch("create_rectangle", {}); !r) {
      log_error(r.error());
    }
  }
}

bool DocumentViewport::finish_pending_if_done(const Result<bool>& done) {
  if (!done) {
    log_error(done.error());
    if (session_->tool_mode() != ToolMode::None) {
      rearm_tool();
    }
    request_redraw();
    return true;
  }
  if (*done) {
    resync_all_meshes();
    rebuild_bvh();
    emit document_changed();
    // 画完一个实体后继续同一绘制命令，直到 Esc / 右键退出。
    if (session_->tool_mode() != ToolMode::None) {
      rearm_tool();
    }
    request_redraw();
    return true;
  }
  return false;
}

void DocumentViewport::cancel_tool() {
  unsetCursor();
  if (plugin_point_input_.active()) {
    cancel_plugin_point_input();
    request_redraw();
  } else if (session_->tool_mode() == ToolMode::None &&
             command_system_.has_pending()) {
    command_system_.cancel();
    request_redraw();
  } else if (session_->tool_mode() != ToolMode::None && command_system_.drag_started()) {
    command_system_.cancel();
    dispatch_tool_command(session_->tool_mode());  // 取消当前放置，仍留在工具
    request_redraw();
  } else if (session_->tool_mode() != ToolMode::None) {
    set_tool(ToolMode::None);  // 退出工具
  }
}

void DocumentViewport::adjust_selected_param(double delta) {
  Entity* entity = document_->selected_entity();
  if (entity == nullptr) {
    return;
  }
  // 找可编辑的轮廓特征：RectProfile / PolygonProfile 的 width，或 CircleProfile 的 radius。
  std::uint64_t feature_id = 0;
  std::string param_name;
  double current = 0.0;
  for (const auto& f : entity->model.features()) {
    if (f.kind == FeatureKind::RectProfile || f.kind == FeatureKind::PolygonProfile) {
      feature_id = f.id;
      param_name = "width";
      current = entity->model.param(f.id, "width", 0.0);
      break;
    }
    if (f.kind == FeatureKind::CircleProfile || f.kind == FeatureKind::CircleWire) {
      feature_id = f.id;
      param_name = "radius";
      current = entity->model.param(f.id, "radius", 0.0);
      break;
    }
  }
  if (feature_id == 0) {
    return;
  }
  run_command("set_param",
              {{"entity_id", static_cast<std::int64_t>(entity->id)},
               {"feature_id", static_cast<std::int64_t>(feature_id)},
               {"param_name", param_name},
               {"value", current + delta}});
}

void DocumentViewport::run_command(const std::string& name, const CommandArgs& args,
                                   bool notify) {
  if (auto r = session_->dispatch(name, args); r) {
    resync_all_meshes();
    document_->recompute_scene();
    rebuild_bvh();
    request_redraw();
    if (notify) {
      emit document_changed();
    }
  } else {
    log_error(r.error());
  }
}

void DocumentViewport::refresh_after_edit() {
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
  emit selection_changed();
}

void DocumentViewport::notify_selection_changed() {
  request_redraw();
  emit selection_changed();
}

Result<void> DocumentViewport::begin_plugin_point_input(
    PluginPointInputRequest request, PluginHost::PointInputCompletion completion) {
  set_tool(ToolMode::None);
  setFocus();
  auto started = plugin_point_input_.begin(
      std::move(request),
      [this, completion = std::move(completion)](
          std::vector<PluginPickPoint> points, bool cancelled) mutable {
        emit plugin_point_input_changed(false);
        completion(std::move(points), cancelled);
        request_redraw();
      });
  if (started) {
    emit plugin_point_input_changed(true);
    request_redraw();
  }
  return started;
}

void DocumentViewport::cancel_plugin_point_input(std::uint64_t request_id) {
  plugin_point_input_.cancel(request_id);
  request_redraw();
}

void DocumentViewport::set_entity_param(std::uint64_t entity_id, std::uint64_t feature_id,
                                        const std::string& param_name, double value) {
  // 属性面板自己已经在 spinbox 里展示了新值，无需再通知刷新（避免重建丢焦点）。
  run_command("set_param",
              {{"entity_id", static_cast<std::int64_t>(entity_id)},
               {"feature_id", static_cast<std::int64_t>(feature_id)},
               {"param_name", param_name},
               {"value", value}},
              /*notify=*/false);
}

void DocumentViewport::set_entity_material(std::uint64_t entity_id, const Material& material) {
  // 纯视觉：材质只影响渲染，不重建网格/场景/BVH，仅重绘一帧。
  // 属性面板自己已展示新值，无需 notify 刷新（避免重建丢焦点）。
  if (auto r = session_->dispatch(
          "set_material",
          {{"entity_id", static_cast<std::int64_t>(entity_id)},
           {"material_id", static_cast<std::int64_t>(material.id)},
           {"name", material.name},
           {"base_color", material.base_color},
           {"roughness", static_cast<double>(material.roughness)},
           {"metallic", static_cast<double>(material.metallic)},
           {"opacity", static_cast<double>(material.opacity)},
           {"albedo_texture_id", static_cast<std::int64_t>(material.albedo_texture_id)},
           {"normal_texture_id", static_cast<std::int64_t>(material.normal_texture_id)},
           {"orm_texture_id", static_cast<std::int64_t>(material.orm_texture_id)},
           {"tex_scale_x", static_cast<double>(material.tex.scale.x)},
           {"tex_scale_y", static_cast<double>(material.tex.scale.y)},
           {"tex_offset_x", static_cast<double>(material.tex.offset.x)},
           {"tex_offset_y", static_cast<double>(material.tex.offset.y)},
           {"tex_rotation", static_cast<double>(material.tex.rotation)},
           {"tex_world_scale", static_cast<double>(material.tex.world_scale)}});
      r) {
    request_redraw();
  } else {
    log_error(r.error());
  }
}

std::uint64_t DocumentViewport::import_texture(TextureAsset asset) {
  auto cmd = std::make_unique<ImportTextureCommand>(*document_, std::move(asset));
  ImportTextureCommand* raw = cmd.get();
  if (auto r = raw->execute(); !r) {
    log_error(r.error());
    return 0;
  }
  const std::uint64_t id = raw->texture_id();
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
  emit document_changed();
  return id;
}

void DocumentViewport::replace_texture(std::uint64_t id, TextureAsset asset) {
  auto cmd = std::make_unique<ReplaceTextureCommand>(*document_, id, std::move(asset));
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
  emit document_changed();
}

void DocumentViewport::update_library_material(const Material& material) {
  auto cmd = std::make_unique<UpdateMaterialCommand>(*document_, material);
  if (auto r = cmd->execute(); !r) {
    log_error(r.error());
    return;
  }
  session_->command_system().push_executed(std::move(cmd));
  request_redraw();
}

void DocumentViewport::create_storey(const std::string& name, double elevation) {
  run_command("create_storey", {{"name", name}, {"elevation", elevation}});
}

void DocumentViewport::set_active_storey(std::uint64_t storey_id) {
  document_->set_active_storey(storey_id);
  request_redraw();
  emit document_changed();
}

void DocumentViewport::set_entity_location(std::uint64_t entity_id, std::uint64_t storey_id,
                                           double elevation_offset) {
  run_command("set_location",
              {{"entity_id", static_cast<std::int64_t>(entity_id)},
               {"storey_id", static_cast<std::int64_t>(storey_id)},
               {"elevation_offset", elevation_offset}},
              /*notify=*/false);
}

void DocumentViewport::fillet_selected(double radius) {
  const Entity* e = document_->selected_entity();
  if (e == nullptr) {
    log_error("fillet: no entity selected");
    return;
  }
  run_command("fillet", {{"entity_id", static_cast<std::int64_t>(e->id)},
                         {"radius", radius},
                         {"edge", static_cast<std::int64_t>(0)}});
}

void DocumentViewport::chamfer_selected(double distance) {
  const Entity* e = document_->selected_entity();
  if (e == nullptr) {
    log_error("chamfer: no entity selected");
    return;
  }
  run_command("chamfer", {{"entity_id", static_cast<std::int64_t>(e->id)},
                          {"distance", distance},
                          {"edge", static_cast<std::int64_t>(0)}});
}

void DocumentViewport::delete_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  if (ids.empty()) {
    return;
  }
  for (const std::uint64_t id : ids) {
    if (document_->entity(id) == nullptr) {
      continue;
    }
    run_command("delete_entity", {{"entity_id", static_cast<std::int64_t>(id)}});
  }
  emit selection_changed();
}

std::uint64_t DocumentViewport::pick_node_at(const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        return node_visible_in_view(id);
      })) {
    return hit->node_id;
  }
  return 0;
}

std::optional<std::pair<std::uint64_t, Vec3>> DocumentViewport::pick_wall_at(
    const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  if (auto hit = bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) {
        if (!node_visible_in_view(id)) {
          return false;
        }
        const Entity* entity = document_->entity(id);
        return entity != nullptr && is_wall_host(*entity);
      })) {
    return std::pair<std::uint64_t, Vec3>{hit->node_id, ray.origin + ray.direction * hit->t};
  }
  return std::nullopt;
}

bool DocumentViewport::is_opening_placement_tool() const {
  return session_->tool_mode() == ToolMode::Window || session_->tool_mode() == ToolMode::Door;
}

void DocumentViewport::update_opening_hover(const QPoint& pos) {
  if (!is_opening_placement_tool() || !command_system_.has_pending()) {
    return;
  }
  if (auto wall = pick_wall_at(pos)) {
    command_system_.hover(wall->second, wall->first);
    setCursor(Qt::CrossCursor);
  } else {
    command_system_.hover(cursor_ground_position(pos), 0);
    setCursor(Qt::ForbiddenCursor);
  }
}

void DocumentViewport::show_entity_context_menu(const QPoint& global_pos) {
  if (document_->selected_entity() == nullptr) {
    return;
  }
  QMenu menu(this);
  QAction* hide_act = menu.addAction(tr("Hide"));
  QAction* isolate_act = menu.addAction(tr("Isolate"));
  menu.addSeparator();
  QAction* delete_act = menu.addAction(tr("Delete"));
  delete_act->setShortcut(QKeySequence::Delete);
  QAction* chosen = menu.exec(global_pos);
  if (chosen == hide_act) {
    hide_selected();
  } else if (chosen == isolate_act) {
    isolate_selected();
  } else if (chosen == delete_act) {
    delete_selected();
  }
}

void DocumentViewport::resync_all_meshes() {
  if (!render_thread_) {
    return;
  }
  for (const auto& [unused, asset] : document_->meshes()) {
    (void)unused;
    if (asset.cpu.vertices.empty() || asset.cpu.indices.empty()) {
      continue;
    }
    render_thread_->request_upload_mesh(asset.id, asset.cpu);
  }
}

void DocumentViewport::resync_textures() {
  if (!render_thread_) {
    return;
  }
  auto forget_evicted = [this]() {
    for (const std::uint64_t id : render_thread_->take_evicted_texture_ids()) {
      uploaded_textures_.erase(id);
    }
  };
  forget_evicted();
  for (const auto& [id, asset] : document_->textures()) {
    if (auto it = uploaded_textures_.find(id);
        it != uploaded_textures_.end() && it->second == asset.generation) {
      continue;
    }
    if (auto gpu = render_thread_->upload_texture(id, asset); !gpu) {
      log_error(gpu.error());
    } else {
      uploaded_textures_[id] = asset.generation;
    }
  }
  forget_evicted();
}

void DocumentViewport::undo() {
  if (!session_->can_undo()) {
    return;
  }
  session_->undo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

void DocumentViewport::redo() {
  if (!session_->can_redo()) {
    return;
  }
  session_->redo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

bool DocumentViewport::grid_snap_active() const {
  if (plugin_point_input_.active()) {
    return plugin_point_input_.grid_snap();
  }
  if (session_->tool_mode() == ToolMode::None) {
    return command_system_.has_pending();
  }
  return session_->tool_mode() != ToolMode::None && session_->tool_mode() != ToolMode::Door &&
         session_->tool_mode() != ToolMode::Window;
}

Vec3 DocumentViewport::cursor_ground_position(const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  Vec3 hit = ray.origin + ray.direction * camera_.distance();
  // 与当前工作面求交（地面 y=0；画板时是板的标高，避免透视下点偏）。
  const float plane_y = plugin_point_input_.active()
                            ? plugin_point_input_.work_plane_y()
                            : command_system_.work_plane_y();
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      hit = ray.origin + ray.direction * t;
    }
  }
  if (grid_snap_active()) {
    return snapped_ground_position(pos);
  }
  return hit;
}

Vec3 DocumentViewport::snapped_ground_position(const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  Vec3 hit = ray.origin + ray.direction * camera_.distance();
  const float plane_y = plugin_point_input_.active()
                            ? plugin_point_input_.work_plane_y()
                            : command_system_.work_plane_y();
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = (plane_y - ray.origin.y) / ray.direction.y;
    if (t > 0.f) {
      hit = ray.origin + ray.direction * t;
    }
  }
  const float dist = length(hit - camera_.eye_position());
  const float radius =
      grid_snap_world_radius(dist, camera_.fovy(), static_cast<float>((std::max)(1, height())));
  return snap_to_grid_xz_if_near(hit, radius);
}

Mat4 DocumentViewport::view_proj() const {
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  return camera_.proj_matrix(aspect) * camera_.view_matrix();
}

void DocumentViewport::update_box_select_rect(const QPoint& pos) {
  if (box_select_overlay_ == nullptr) {
    return;
  }
  const bool crossing = pos.x() < press_mouse_.x();
  box_select_overlay_->set_box(QRect(press_mouse_, pos), crossing);
}

void DocumentViewport::finish_box_select(const QPoint& pos, bool additive) {
  if (box_select_overlay_ != nullptr) {
    box_select_overlay_->hide_box();
  }
  if ((pos - press_mouse_).manhattanLength() < 4) {
    return;
  }
  const bool crossing = pos.x() < press_mouse_.x();
  const std::vector<std::uint64_t> ids = nodes_in_screen_rect(
      *document_, view_proj(), static_cast<float>((std::max)(1, width())),
      static_cast<float>((std::max)(1, height())), static_cast<float>(press_mouse_.x()),
      static_cast<float>(press_mouse_.y()), static_cast<float>(pos.x()),
      static_cast<float>(pos.y()), crossing);
  if (!additive) {
    session_->clear_selection();
  }
  for (const std::uint64_t id : ids) {
    if (node_visible_in_view(id)) {
      session_->select(id);
    }
  }
  request_redraw();
  emit selection_changed();
}

void DocumentViewport::set_plan_view(bool plan, bool restore_perspective) {
  if (plan_view_ == plan) {
    if (tool_strip_) {
      tool_strip_->set_plan_view(plan_view_);
    }
    return;
  }
  if (plan) {
    persp_yaw_ = camera_.yaw();
    persp_pitch_ = camera_.pitch();
    plan_view_ = true;
    start_view_animation(0.f, kHalfPi, true);
  } else {
    plan_view_ = false;
    camera_.set_orthographic(false);
    if (restore_perspective) {
      start_view_animation(persp_yaw_, persp_pitch_);
    }
  }
  if (tool_strip_) {
    tool_strip_->set_plan_view(plan_view_);
  }
  if (!plan_view_ && session_->tool_mode() == ToolMode::Slab) {
    set_tool(ToolMode::None);
    refuse_slab_outside_plan(false);
  }
  request_redraw();
}

void DocumentViewport::hide_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  if (ids.empty()) {
    return;
  }
  for (const std::uint64_t id : ids) {
    hidden_ids_.insert(id);
    isolated_ids_.erase(id);
  }
  request_redraw();
}

void DocumentViewport::isolate_selected() {
  const std::vector<std::uint64_t> ids = document_->selected_ids();
  if (ids.empty()) {
    return;
  }
  isolated_ids_.clear();
  for (const std::uint64_t id : ids) {
    isolated_ids_.insert(id);
    hidden_ids_.erase(id);
  }
  request_redraw();
}

void DocumentViewport::show_all_visible() {
  hidden_ids_.clear();
  isolated_ids_.clear();
  hidden_kinds_.clear();
  request_redraw();
}

void DocumentViewport::set_kind_hidden(EntityKind kind, bool hidden) {
  if (hidden) {
    hidden_kinds_.insert(kind);
  } else {
    hidden_kinds_.erase(kind);
  }
  request_redraw();
}

void DocumentViewport::set_active_floor(int index) {
  refresh_floors();
  if (index >= static_cast<int>(floors_.size())) {
    index = -1;
  }
  if (active_floor_ == index) {
    return;
  }
  active_floor_ = index;
  request_redraw();
}

void DocumentViewport::refresh_floors() {
  floors_ = infer_viewport_floors(*document_);
  if (active_floor_ >= static_cast<int>(floors_.size())) {
    active_floor_ = -1;
  }
}

bool DocumentViewport::node_visible_in_view(std::uint64_t id) const {
  if (hidden_ids_.count(id) != 0) {
    return false;
  }
  if (!isolated_ids_.empty() && isolated_ids_.count(id) == 0) {
    return false;
  }
  const Entity* entity = document_->entity(id);
  if (entity != nullptr && hidden_kinds_.count(entity->kind()) != 0) {
    return false;
  }
  if (active_floor_ >= 0 && active_floor_ < static_cast<int>(floors_.size())) {
    const SceneNode* node = document_->scene().find(id);
    if (node != nullptr &&
        !viewport_floor_contains(floors_[static_cast<std::size_t>(active_floor_)],
                                 node->world_bounds)) {
      return false;
    }
  }
  return true;
}

void DocumentViewport::populate_visibility_menu() {
  if (tool_strip_ == nullptr) {
    return;
  }
  QMenu* menu = tool_strip_->visibility_menu();
  menu->clear();
  QAction* hide_act = menu->addAction(tr("Hide Selected"));
  hide_act->setEnabled(!document_->selected_ids().empty());
  connect(hide_act, &QAction::triggered, this, &DocumentViewport::hide_selected);
  QAction* isolate_act = menu->addAction(tr("Isolate Selected"));
  isolate_act->setEnabled(!document_->selected_ids().empty());
  connect(isolate_act, &QAction::triggered, this, &DocumentViewport::isolate_selected);
  QAction* show_act = menu->addAction(tr("Show All"));
  show_act->setEnabled(!hidden_ids_.empty() || !isolated_ids_.empty() || !hidden_kinds_.empty());
  connect(show_act, &QAction::triggered, this, &DocumentViewport::show_all_visible);

  std::unordered_set<EntityKind> present;
  for (const auto& [unused, entity] : document_->entities()) {
    (void)unused;
    if (entity) {
      present.insert(entity->kind());
    }
  }
  if (present.empty()) {
    return;
  }
  menu->addSeparator();
  QMenu* cats = menu->addMenu(tr("Categories"));
  const auto add_kind = [this, cats, &present](EntityKind kind, const QString& label,
                                               const QString& icon) {
    if (present.count(kind) == 0) {
      return;
    }
    QAction* act = cats->addAction(QIcon(icon), label);
    act->setCheckable(true);
    act->setChecked(hidden_kinds_.count(kind) == 0);
    connect(act, &QAction::toggled, this, [this, kind](bool visible) {
      set_kind_hidden(kind, !visible);
    });
  };
  add_kind(EntityKind::Wall, tr("Walls"), QStringLiteral(":/icons/wall.svg"));
  add_kind(EntityKind::Beam, tr("Beams"), QStringLiteral(":/icons/beam.svg"));
  add_kind(EntityKind::Column, tr("Columns"), QStringLiteral(":/icons/column.svg"));
  add_kind(EntityKind::Slab, tr("Slabs"), QStringLiteral(":/icons/slab.svg"));
  add_kind(EntityKind::Door, tr("Doors"), QStringLiteral(":/icons/door.svg"));
  add_kind(EntityKind::Window, tr("Windows"), QStringLiteral(":/icons/window.svg"));
  add_kind(EntityKind::Box, tr("Boxes"), QStringLiteral(":/icons/box.svg"));
  add_kind(EntityKind::Cylinder, tr("Cylinders"), QStringLiteral(":/icons/cylinder.svg"));
  add_kind(EntityKind::Line, tr("Lines"), QStringLiteral(":/icons/line.svg"));
  add_kind(EntityKind::Polyline, tr("Polylines"), QStringLiteral(":/icons/polyline.svg"));
  add_kind(EntityKind::Circle, tr("Circles"), QStringLiteral(":/icons/circle.svg"));
  add_kind(EntityKind::Arc, tr("Arcs"), QStringLiteral(":/icons/arc.svg"));
  add_kind(EntityKind::Bezier, tr("Beziers"), QStringLiteral(":/icons/bezier.svg"));
  add_kind(EntityKind::BSpline, tr("B-splines"), QStringLiteral(":/icons/bspline.svg"));
  add_kind(EntityKind::Nurbs, tr("NURBS"), QStringLiteral(":/icons/nurbs.svg"));
  add_kind(EntityKind::Rectangle, tr("Rectangles"), QStringLiteral(":/icons/rectangle.svg"));
}

void DocumentViewport::populate_floor_menu() {
  if (tool_strip_ == nullptr) {
    return;
  }
  refresh_floors();
  QMenu* menu = tool_strip_->floor_menu();
  menu->clear();

  QAction* current_header = menu->addAction(tr("Current Storey"));
  current_header->setEnabled(false);
  auto* storey_group = new QActionGroup(menu);
  storey_group->setExclusive(true);
  QAction* unassigned = menu->addAction(tr("Unassigned"));
  unassigned->setCheckable(true);
  unassigned->setChecked(document_->bim().active_storey_id() == 0);
  storey_group->addAction(unassigned);
  connect(unassigned, &QAction::triggered, this, [this] { set_active_storey(0); });
  for (const Storey& storey : document_->bim().storeys()) {
    QAction* action = menu->addAction(
        tr("%1  (%2 m)").arg(QString::fromStdString(storey.name)).arg(storey.elevation, 0, 'f', 3));
    action->setCheckable(true);
    action->setChecked(document_->bim().active_storey_id() == storey.id);
    storey_group->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, id = storey.id] { set_active_storey(id); });
  }
  QAction* create = menu->addAction(tr("New Storey..."));
  connect(create, &QAction::triggered, this, [this] {
    bool accepted = false;
    const QString name =
        QInputDialog::getText(this, tr("New Storey"), tr("Name"), QLineEdit::Normal,
                              tr("Storey"), &accepted);
    if (!accepted || name.trimmed().isEmpty()) {
      return;
    }
    const double elevation =
        QInputDialog::getDouble(this, tr("New Storey"), tr("Elevation (m)"), 0.0,
                                -1000000.0, 1000000.0, 3, &accepted);
    if (accepted) {
      create_storey(name.trimmed().toStdString(), elevation);
    }
  });

  menu->addSeparator();
  QAction* filter_header = menu->addAction(tr("Visibility Filter"));
  filter_header->setEnabled(false);
  auto* group = new QActionGroup(menu);
  group->setExclusive(true);

  QAction* all = menu->addAction(tr("All Floors"));
  all->setCheckable(true);
  all->setChecked(active_floor_ < 0);
  group->addAction(all);
  connect(all, &QAction::triggered, this, [this] { set_active_floor(-1); });

  if (floors_.empty()) {
    QAction* empty = menu->addAction(tr("No floors in this model"));
    empty->setEnabled(false);
    return;
  }
  menu->addSeparator();
  for (int i = 0; i < static_cast<int>(floors_.size()); ++i) {
    QAction* act = menu->addAction(QString::fromStdString(floors_[static_cast<std::size_t>(i)].label));
    act->setCheckable(true);
    act->setChecked(active_floor_ == i);
    group->addAction(act);
    connect(act, &QAction::triggered, this, [this, i] { set_active_floor(i); });
  }
}

bool DocumentViewport::pick_grip_at(const QPoint& pos, EntityGrip& out) const {
  if (session_->tool_mode() != ToolMode::None || document_ == nullptr) {
    return false;
  }
  const Mat4 vp = view_proj();
  const float w = static_cast<float>((std::max)(1, width()));
  const float h = static_cast<float>((std::max)(1, height()));
  float best = 10.f;
  bool hit = false;
  for (const std::uint64_t id : document_->selected_ids()) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    for (const EntityGrip& grip : collect_entity_grips(*entity)) {
      float sx = 0.f;
      float sy = 0.f;
      if (!project_world_to_screen(vp, grip.world, w, h, sx, sy)) {
        continue;
      }
      const float dx = sx - static_cast<float>(pos.x());
      const float dy = sy - static_cast<float>(pos.y());
      const float d = std::sqrt(dx * dx + dy * dy);
      if (d < best) {
        best = d;
        out = grip;
        hit = true;
      }
    }
  }
  return hit;
}

void DocumentViewport::clear_grip_preview() {
  grip_preview_polyline_.clear();
  grip_preview_points_.clear();
  grip_to_model_ = {};
  grip_to_transform_ = Mat4::identity();
  grip_preview_valid_ = false;
}

void DocumentViewport::apply_grip_at(const QPoint& pos) {
  Entity* entity = document_->entity(active_grip_.entity_id);
  if (entity == nullptr) {
    return;
  }
  Vec3 p = snapped_ground_position(pos);
  p.y = grip_from_world_.y;
  // Preview on a clone. Live document stays at the press-time shape until drop;
  // createGeom / EditEntityGripCommand run once in commit_grip_drag.
  // Always reshape from the press-time snapshot: canonical corner indices remap
  // after a rebuild, and applying incrementally would pin the wrong opposite
  // once the dragged corner crosses through it.
  std::unique_ptr<Entity> preview = entity->clone();
  preview->model = grip_from_model_;
  preview->local_transform = grip_from_transform_;
  if (!apply_entity_grip(*preview, active_grip_.index, p)) {
    return;
  }
  grip_to_model_ = preview->model;
  grip_to_transform_ = preview->local_transform;
  grip_preview_polyline_ = tamias::grip_preview_polyline(*preview);
  grip_preview_points_.clear();
  for (const EntityGrip& g : collect_entity_grips(*preview)) {
    grip_preview_points_.push_back(g.world);
  }
  grip_preview_valid_ = true;
  request_redraw();
}

void DocumentViewport::commit_grip_drag() {
  Entity* entity = document_->entity(active_grip_.entity_id);
  if (entity == nullptr || !grip_preview_valid_) {
    clear_grip_preview();
    return;
  }
  Vec3 to_world = grip_from_world_;
  if (active_grip_.index >= 0 &&
      active_grip_.index < static_cast<int>(grip_preview_points_.size())) {
    to_world = grip_preview_points_[static_cast<std::size_t>(active_grip_.index)];
  }
  if (length(to_world - grip_from_world_) < 1e-4f) {
    clear_grip_preview();
    return;
  }
  auto command = std::make_unique<EditEntityGripCommand>(
      *document_, entity->id, grip_from_model_, grip_from_transform_, grip_to_model_,
      grip_to_transform_);
  {
    TimingScope scope("edit_entity_grip", TimingCategory::Command);
    if (auto r = command->execute(); !r) {
      log_error(r.error());
      clear_grip_preview();
      return;
    }
  }
  command_system_.push_executed(std::move(command));
  resync_all_meshes();
  rebuild_bvh();
  clear_grip_preview();
  emit document_changed();
  request_redraw();
}

void DocumentViewport::fill_grip_overlay(FrameSubmission& frame) const {
  if (session_->tool_mode() != ToolMode::None || document_ == nullptr) {
    return;
  }
  if (gripping_ && grip_preview_polyline_.size() >= 2) {
    frame.preview_polyline = grip_preview_polyline_;
  }
  for (const std::uint64_t id : document_->selected_ids()) {
    const Entity* entity = document_->entity(id);
    if (entity == nullptr) {
      continue;
    }
    std::vector<Vec3> worlds;
    if (gripping_ && id == active_grip_.entity_id && !grip_preview_points_.empty()) {
      worlds = grip_preview_points_;
    } else {
      for (const EntityGrip& g : collect_entity_grips(*entity)) {
        worlds.push_back(g.world);
      }
    }
    if (worlds.empty()) {
      continue;
    }
    if ((entity->kind() == EntityKind::Bezier || entity->kind() == EntityKind::BSpline ||
         entity->kind() == EntityKind::Nurbs) &&
        frame.preview_control_polyline.empty()) {
      frame.preview_control_polyline = worlds;
    }
    for (const Vec3& p : worlds) {
      frame.grip_points.push_back(p);
    }
  }
}

void DocumentViewport::set_debug_overlay(std::optional<Aabb> aabb,
                                         std::optional<std::uint64_t> isolate_node) {
  debug_aabb_ = std::move(aabb);
  debug_isolate_node_ = isolate_node;
  request_redraw();
}

void DocumentViewport::set_debug_vertex(std::optional<DebugVertexOverlay> vertex) {
  debug_vertex_ = std::move(vertex);
  request_redraw();
}

void DocumentViewport::fill_debug_overlay(FrameSubmission& frame) const {
  if (debug_aabb_ && debug_aabb_->valid()) {
    const Aabb& box = *debug_aabb_;
    const Vec3 c[8] = {
        {box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z},
        {box.min.x, box.max.y, box.min.z}, {box.max.x, box.max.y, box.min.z},
        {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z},
        {box.min.x, box.max.y, box.max.z}, {box.max.x, box.max.y, box.max.z},
    };
    const int edges[12][2] = {{0, 1}, {1, 3}, {3, 2}, {2, 0}, {4, 5}, {5, 7},
                              {7, 6}, {6, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    frame.debug_line_segments.reserve(24);
    for (const auto& e : edges) {
      frame.debug_line_segments.push_back(c[e[0]]);
      frame.debug_line_segments.push_back(c[e[1]]);
    }
  }
  frame.debug_vertex = debug_vertex_;
}

}  // namespace tamias
