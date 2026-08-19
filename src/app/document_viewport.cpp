#include "document_viewport.h"

#include "app_settings.h"
#include "engine/core/log.h"
#include "engine/modeling/curve_geom.h"
#include "engine/modeling/feature.h"

#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <QAction>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

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
      document_(std::move(document)),
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
    camera_.orbit(dyaw, dpitch);
    request_redraw();
  });

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

  camera_.frame_aabb(document_->bounds().valid() ? document_->bounds()
                                                 : Aabb{{-1, -1, -1}, {1, 1, 1}});
  rebuild_bvh();
  sync_view_cube();
  sync_coord_readout();
}

DocumentViewport::~DocumentViewport() {
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

void DocumentViewport::apply_viewport_state(const ViewportState& state) {
  stop_view_animation();
  camera_.set_target(state.target);
  camera_.set_distance(state.distance);
  camera_.set_yaw_pitch(state.yaw, state.pitch);
  camera_.set_fovy(state.fovy);
  // Recompute clip planes from distance so stale znear/zfar cannot clip the scene.
  const float distance = camera_.distance();
  camera_.set_znear(std::max(distance * 0.001f, 0.01f));
  camera_.set_zfar(std::max(distance * 20.f, 100.f));
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
  if (coord_label_) {
    coord_label_->adjustSize();
    coord_label_->move(kMargin, height() - coord_label_->height() - kMargin);
    coord_label_->raise();
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

  if (auto hit = bvh_.closest_hit(ray, *document_)) {
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
  if (!has_cursor_) {
    const Vec3 target = camera_.target();
    coord_label_->setText(tr("X %1  Y %2  Z %3")
                              .arg(target.x, 0, 'f', 3)
                              .arg(target.y, 0, 'f', 3)
                              .arg(target.z, 0, 'f', 3));
  } else {
    const Vec3 p = cursor_world_position(last_mouse_);
    coord_label_->setText(tr("X %1  Y %2  Z %3")
                              .arg(p.x, 0, 'f', 3)
                              .arg(p.y, 0, 'f', 3)
                              .arg(p.z, 0, 'f', 3));
  }
  layout_overlays();
}

void DocumentViewport::stop_view_animation() {
  if (view_anim_timer_ && view_anim_timer_->isActive()) {
    view_anim_timer_->stop();
  }
}

void DocumentViewport::start_view_animation(float target_yaw, float target_pitch) {
  constexpr float kPi = 3.141592654f;
  anim_from_yaw_ = camera_.yaw();
  anim_from_pitch_ = camera_.pitch();
  anim_to_yaw_ = target_yaw;
  anim_to_pitch_ = std::clamp(target_pitch, -1.5f, 1.5f);

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
    stop_view_animation();
    request_redraw();
    return;
  }

  view_anim_clock_.restart();
  view_anim_timer_->start();
  on_view_anim_tick();
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
      pitch = 1.5f;
      break;
    case ViewCubeFace::Bottom:
      pitch = -1.5f;
      break;
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
  frame.mode = mode_;
  const Frustum frustum = Frustum::from_view_proj(frame.proj * frame.view);
  frame.items = document_->render_items(&frustum);
  const Vec3 cursor = has_cursor_ ? cursor_ground_position(last_mouse_) : Vec3{};
  frame.preview_polyline = command_system_.preview_polyline(cursor);
  frame.preview_control_polyline = command_system_.preview_control_polyline(cursor);
  frame.preview_points = command_system_.preview_points(cursor);
  if (frame.preview_control_polyline.empty() && frame.preview_points.empty() && document_) {
    if (const Entity* entity = document_->selected_entity();
        entity != nullptr && entity->kind() == EntityKind::Bezier) {
      if (const Feature* feature = entity->model.output_feature();
          feature != nullptr && feature->kind == FeatureKind::Bezier) {
        const std::vector<Vec3> local = bezier_control_points(entity->model, *feature);
        frame.preview_control_polyline.reserve(local.size());
        frame.preview_points.reserve(local.size());
        for (const Vec3& p : local) {
          const Vec3 world = entity->local_transform * p;
          frame.preview_control_polyline.push_back(world);
          frame.preview_points.push_back(world);
        }
      }
    }
  }
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
  if (event->button() == Qt::LeftButton) {
    if (tool_mode_ != ToolMode::None) {
      Vec3 point = cursor_ground_position(event->pos());
      std::uint64_t picked = 0;
      if (tool_mode_ == ToolMode::Window || tool_mode_ == ToolMode::Door) {
        const auto dpr = devicePixelRatioF();
        const float aspect = static_cast<float>((std::max)(1, width())) /
                             static_cast<float>((std::max)(1, height()));
        const Ray ray =
            camera_ray(camera_, aspect, static_cast<float>(event->pos().x() * dpr),
                       static_cast<float>(event->pos().y() * dpr),
                       static_cast<float>(width() * dpr), static_cast<float>(height() * dpr));
        if (auto hit = bvh_.closest_hit(ray, *document_)) {
          if (const Entity* host = document_->entity(hit->node_id);
              host != nullptr && host->kind() == EntityKind::Wall) {
            picked = host->id;
            point = ray.origin + ray.direction * hit->t;
          }
        }
      }
      auto r = command_system_.feed_point(point, picked);  // 喂交互点给 pending 命令
      if (finish_pending_if_done(r)) {
        return;
      }
      request_redraw();
      return;
    }
    stop_view_animation();
    orbiting_ = true;
  } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    stop_view_animation();
    panning_ = true;
  }
}

void DocumentViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPoint delta = event->pos() - last_mouse_;
  last_mouse_ = event->pos();
  has_cursor_ = true;
  if (orbiting_) {
    camera_.orbit(-delta.x() * 0.01f, delta.y() * 0.01f);
    request_redraw();
  } else if (panning_) {
    const float scale = camera_.distance() * 0.002f;
    camera_.pan(-delta.x() * scale, delta.y() * scale);
    request_redraw();
  } else if (command_system_.drag_started()) {
    request_redraw();  // 更新预览线
  } else {
    sync_coord_readout();
  }
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    orbiting_ = false;
    if ((event->pos() - press_mouse_).manhattanLength() < 4) {
      document_->clear_selection();
      if (const std::uint64_t hit = pick_node_at(event->pos()); hit != 0) {
        document_->select(hit);
      }
      request_redraw();
      emit selection_changed();
    }
  }
  if (event->button() == Qt::MiddleButton) {
    panning_ = false;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = false;
    if (command_system_.accepts_confirm() &&
        (event->pos() - press_mouse_).manhattanLength() < 4) {
      finish_pending_if_done(command_system_.confirm());
      return;
    }
    if (tool_mode_ == ToolMode::None && (event->pos() - press_mouse_).manhattanLength() < 4) {
      if (const std::uint64_t hit = pick_node_at(event->pos());
          hit != 0 && document_->entity(hit) != nullptr) {
        document_->clear_selection();
        document_->select(hit);
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
  if (event->button() != Qt::LeftButton || !command_system_.accepts_confirm()) {
    QWidget::mouseDoubleClickEvent(event);
    return;
  }
  last_mouse_ = event->pos();
  finish_pending_if_done(command_system_.confirm());
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
    const Vec3 old_target = camera_.target();
    camera_.dolly(factor);
    camera_.set_target(focus + (old_target - focus) * factor);
  } else {
    camera_.dolly(factor);
  }
  request_redraw();
}

void DocumentViewport::keyPressEvent(QKeyEvent* event) {
  switch (event->key()) {
    case Qt::Key_Escape:
      cancel_tool();
      break;
    case Qt::Key_Delete:
      if (tool_mode_ == ToolMode::None) {
        delete_selected();
      }
      break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
      if (command_system_.accepts_confirm()) {
        finish_pending_if_done(command_system_.confirm());
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

void DocumentViewport::set_tool(ToolMode mode) {
  tool_mode_ = mode;
  if (mode != ToolMode::None) {
    command_system_.cancel();  // 取消之前的 pending
    setFocus();
    dispatch_tool_command(mode);  // 点按钮 → 立即 dispatch 命令（armed）
  } else {
    command_system_.cancel();
  }
  request_redraw();
  emit tool_mode_changed(mode);
}

void DocumentViewport::dispatch_tool_command(ToolMode mode) {
  if (mode == ToolMode::Wall) {
    if (auto r = command_system_.dispatch(*document_, "create_wall",
                                          {{"thickness", 0.2}, {"height", 3.0}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Box) {
    if (auto r = command_system_.dispatch(*document_, "create_box", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Cylinder) {
    if (auto r = command_system_.dispatch(*document_, "create_cylinder", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Beam) {
    if (auto r = command_system_.dispatch(*document_, "create_beam",
                                          {{"width", 0.3}, {"depth", 0.5}});
        !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Column) {
    if (auto r = command_system_.dispatch(*document_, "create_column", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Slab) {
    if (auto r = command_system_.dispatch(*document_, "create_slab", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Door) {
    if (auto r = command_system_.dispatch(*document_, "create_door", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Window) {
    if (auto r = command_system_.dispatch(*document_, "create_window", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Line) {
    if (auto r = command_system_.dispatch(*document_, "create_line", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Polyline) {
    if (auto r = command_system_.dispatch(*document_, "create_polyline", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Circle) {
    if (auto r = command_system_.dispatch(*document_, "create_circle", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Arc) {
    if (auto r = command_system_.dispatch(*document_, "create_arc", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Bezier) {
    if (auto r = command_system_.dispatch(*document_, "create_bezier", {}); !r) {
      log_error(r.error());
    }
  } else if (mode == ToolMode::Rectangle) {
    if (auto r = command_system_.dispatch(*document_, "create_rectangle", {}); !r) {
      log_error(r.error());
    }
  }
}

bool DocumentViewport::finish_pending_if_done(const Result<bool>& done) {
  if (!done) {
    log_error(done.error());
    return true;
  }
  if (*done) {
    set_tool(ToolMode::None);
    resync_all_meshes();
    rebuild_bvh();
    request_redraw();
    emit document_changed();
    return true;
  }
  return false;
}

void DocumentViewport::cancel_tool() {
  if (tool_mode_ != ToolMode::None && command_system_.drag_started()) {
    command_system_.cancel();
    dispatch_tool_command(tool_mode_);  // 取消当前放置，仍留在工具
    request_redraw();
  } else if (tool_mode_ != ToolMode::None) {
    set_tool(ToolMode::None);  // 退出工具
  }
}

void DocumentViewport::adjust_selected_param(double delta) {
  Entity* entity = document_->selected_entity();
  if (entity == nullptr) {
    return;
  }
  // 找可编辑的轮廓特征：RectProfile 的 width，或 CircleProfile 的 radius。
  std::uint64_t feature_id = 0;
  std::string param_name;
  double current = 0.0;
  for (const auto& f : entity->model.features()) {
    if (f.kind == FeatureKind::RectProfile) {
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
  if (auto r = command_system_.dispatch(*document_, name, args); r) {
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
  if (auto r = command_system_.dispatch(
          *document_, "set_material",
          {{"entity_id", static_cast<std::int64_t>(entity_id)},
           {"material_id", static_cast<std::int64_t>(material.id)},
           {"name", material.name},
           {"base_color", material.base_color},
           {"roughness", static_cast<double>(material.roughness)},
           {"metallic", static_cast<double>(material.metallic)},
           {"albedo_texture_id", static_cast<std::int64_t>(material.albedo_texture_id)},
           {"normal_texture_id", static_cast<std::int64_t>(material.normal_texture_id)}});
      r) {
    request_redraw();
  } else {
    log_error(r.error());
  }
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
  const Entity* e = document_->selected_entity();
  if (e == nullptr) {
    return;
  }
  run_command("delete_entity", {{"entity_id", static_cast<std::int64_t>(e->id)}});
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
  if (auto hit = bvh_.closest_hit(ray, *document_)) {
    return hit->node_id;
  }
  return 0;
}

void DocumentViewport::show_entity_context_menu(const QPoint& global_pos) {
  if (document_->selected_entity() == nullptr) {
    return;
  }
  QMenu menu(this);
  QAction* delete_act = menu.addAction(tr("Delete"));
  delete_act->setShortcut(QKeySequence::Delete);
  if (menu.exec(global_pos) == delete_act) {
    delete_selected();
  }
}

void DocumentViewport::resync_all_meshes() {
  if (!render_thread_) {
    return;
  }
  for (const auto& [unused, asset] : document_->meshes()) {
    (void)unused;
    if (auto gpu = render_thread_->upload_mesh(asset.id, asset.cpu); !gpu) {
      log_error(gpu.error());
    }
  }
}

void DocumentViewport::resync_textures() {
  if (!render_thread_) {
    return;
  }
  for (const auto& [id, asset] : document_->textures()) {
    if (uploaded_textures_.count(id)) {
      continue;
    }
    if (auto gpu = render_thread_->upload_texture(id, asset); !gpu) {
      log_error(gpu.error());
    } else {
      uploaded_textures_.insert(id);
    }
  }
}

void DocumentViewport::undo() {
  if (!command_system_.can_undo()) {
    return;
  }
  command_system_.undo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

void DocumentViewport::redo() {
  if (!command_system_.can_redo()) {
    return;
  }
  command_system_.redo();
  resync_all_meshes();
  document_->recompute_scene();
  rebuild_bvh();
  request_redraw();
  emit document_changed();
}

Vec3 DocumentViewport::cursor_ground_position(const QPoint& pos) const {
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_, aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  // 与地面平面 y=0 求交。
  if (std::fabs(ray.direction.y) > 1e-6f) {
    const float t = -ray.origin.y / ray.direction.y;
    if (t > 0.f) {
      return ray.origin + ray.direction * t;
    }
  }
  return ray.origin + ray.direction * camera_.distance();
}

}  // namespace tamias
