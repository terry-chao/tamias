#include "replay_viewport.h"

#include "app_settings.h"
#include "engine/core/log.h"
#include "engine/document/picking.h"

#if defined(TAMIAS_HAS_RHI_OPENGL)
#include "engine/render/rhi/opengl/opengl_backend.h"
#endif

#include <QCoreApplication>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>
#include <QShowEvent>
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

class ReplayViewport::NativeSurface final : public QWidget {
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

ReplayViewport::ReplayViewport(std::shared_ptr<RenderThread> render_thread, QWidget* parent)
    : QWidget(parent), render_thread_(std::move(render_thread)) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(36, 46, 61));
  setPalette(pal);

  surface_ = new NativeSurface(this);
  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);
  root->addWidget(surface_);

  view_cube_ = new ViewCubeWidget(this);
  connect(view_cube_, &ViewCubeWidget::face_clicked, this, &ReplayViewport::on_view_cube_face);
  connect(view_cube_, &ViewCubeWidget::corner_clicked, this, &ReplayViewport::on_view_cube_corner);
  connect(view_cube_, &ViewCubeWidget::orbit_dragged, this, [this](float dyaw, float dpitch) {
    camera_.camera().orbit(dyaw, dpitch);
    request_redraw();
  });
}

ReplayViewport::~ReplayViewport() {
  alive_ = false;
  channel_.reset();
  render_thread_.reset();
  destroy_gl_surface();
}

void ReplayViewport::set_scene(RenderScene scene) {
  const RenderScene::View view = scene.view;
  mode_ = view.mode;
  player_.set_scene(std::move(scene));
  apply_render_scene_view(camera_.camera(), view);
  uploaded_meshes_.clear();
  uploaded_textures_.clear();
  upload_resources();
  request_redraw();
}

void ReplayViewport::set_render_mode(RenderMode mode) {
  mode_ = mode;
  request_redraw();
}

void ReplayViewport::frame_scene() {
  camera_.frame_aabb(player_.bounds());
  request_redraw();
}

void ReplayViewport::request_redraw() {
  if (!alive_) {
    return;
  }
  sync_view_cube();
  submit_current_frame();
}

void ReplayViewport::sync_from_player() { request_redraw(); }

void ReplayViewport::layout_overlays() {
  if (surface_) {
    surface_->lower();
    if (width() > 1 && height() > 1) {
      (void)surface_->winId();
    }
  }
  constexpr int kMargin = 12;
  if (view_cube_) {
    view_cube_->move(width() - view_cube_->width() - kMargin, kMargin);
    view_cube_->raise();
  }
}

void ReplayViewport::sync_view_cube() {
  if (view_cube_) {
    view_cube_->set_orientation(camera_.camera().yaw(), camera_.camera().pitch());
  }
}

NativeWindowHandle ReplayViewport::native_handle() const {
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

void ReplayViewport::destroy_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_) {
    destroy_opengl_surface_hwnd(gl_hwnd_);
    gl_hwnd_ = nullptr;
  }
#endif
}

void ReplayViewport::ensure_gl_surface() {
#if defined(_WIN32) && defined(TAMIAS_HAS_RHI_OPENGL)
  if (gl_hwnd_ || !surface_) {
    return;
  }
  if (AppSettings::instance().graphics_backend() != GraphicsBackend::OpenGL) {
    return;
  }
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

void ReplayViewport::ensure_channel() {
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

void ReplayViewport::upload_resources() {
  if (!render_thread_ || !player_.has_scene()) {
    return;
  }
  const RenderScene& scene = player_.scene();
  for (const auto& [id, mesh] : scene.meshes) {
    if (uploaded_meshes_.contains(id) || mesh.vertices.empty() || mesh.indices.empty()) {
      continue;
    }
    render_thread_->request_upload_mesh(id, mesh);
    uploaded_meshes_.insert(id);
  }
  for (const std::uint64_t id : render_thread_->take_evicted_texture_ids()) {
    uploaded_textures_.erase(id);
  }
  for (const auto& [id, tex] : scene.textures) {
    if (auto it = uploaded_textures_.find(id);
        it != uploaded_textures_.end() && it->second == tex.generation) {
      continue;
    }
    if (auto gpu = render_thread_->upload_texture(id, tex); !gpu) {
      log_error(gpu.error());
    } else {
      uploaded_textures_[id] = tex.generation;
    }
  }
}

void ReplayViewport::submit_current_frame() {
  if (!alive_) {
    return;
  }
  ensure_channel();
  if (!channel_ || !surface_) {
    return;
  }
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
  upload_resources();
  FrameSubmission frame =
      player_.make_frame(native_handle(), w, h, camera_.camera(), mode_);
  channel_->submit(std::move(frame));
}

void ReplayViewport::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  layout_overlays();
  request_redraw();
}

void ReplayViewport::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  layout_overlays();
  request_redraw();
}

void ReplayViewport::mousePressEvent(QMouseEvent* event) {
  last_mouse_ = event->pos();
  if (event->button() == Qt::MiddleButton) {
    mmb_nav_ = true;
    return;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = true;
    return;
  }
  if (event->button() == Qt::LeftButton) {
    const std::uint64_t hit = pick_node_at(event->pos());
    emit node_picked(hit);
  }
}

void ReplayViewport::mouseMoveEvent(QMouseEvent* event) {
  const QPoint delta = event->pos() - last_mouse_;
  last_mouse_ = event->pos();
  if (mmb_nav_ && (event->buttons() & Qt::MiddleButton)) {
    if (event->modifiers() & Qt::ShiftModifier) {
      camera_.pan(-delta.x(), delta.y());
    } else {
      camera_.orbit(-delta.x(), delta.y());
    }
    request_redraw();
    return;
  }
  if (panning_) {
    camera_.pan(-delta.x(), delta.y());
    request_redraw();
  }
}

void ReplayViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::MiddleButton) {
    mmb_nav_ = false;
  }
  if (event->button() == Qt::RightButton) {
    panning_ = false;
  }
}

void ReplayViewport::wheelEvent(QWheelEvent* event) {
  const float steps = event->angleDelta().y() / 120.f;
  const float factor = std::pow(0.9f, steps);
  camera_.dolly(factor);
  request_redraw();
}

void ReplayViewport::keyPressEvent(QKeyEvent* event) {
  if (event->key() == Qt::Key_F) {
    frame_scene();
    return;
  }
  QWidget::keyPressEvent(event);
}

std::uint64_t ReplayViewport::pick_node_at(const QPoint& pos) const {
  if (!player_.has_scene()) {
    return 0;
  }
  const auto dpr = devicePixelRatioF();
  const float aspect = static_cast<float>((std::max)(1, width())) /
                       static_cast<float>((std::max)(1, height()));
  const Ray ray =
      camera_ray(camera_.camera(), aspect, static_cast<float>(pos.x() * dpr),
                 static_cast<float>(pos.y() * dpr), static_cast<float>(width() * dpr),
                 static_cast<float>(height() * dpr));
  float best_t = 1e30f;
  std::uint64_t hit = 0;
  const std::vector<std::uint64_t> hidden_ids = player_.hidden_node_ids();
  std::unordered_set<std::uint64_t> hidden(hidden_ids.begin(), hidden_ids.end());
  for (const auto& item : player_.filtered_items()) {
    if (hidden.count(item.node_id) != 0 || !item.bounds.valid()) {
      continue;
    }
    float t = 0.f;
    if (intersect_aabb(ray, item.bounds, t) && t < best_t) {
      best_t = t;
      hit = item.node_id;
    }
  }
  return hit;
}

void ReplayViewport::on_view_cube_face(ViewCubeFace face) {
  TurntableCamera& cam = camera_.camera();
  switch (face) {
    case ViewCubeFace::Front:
      cam.look_front();
      break;
    case ViewCubeFace::Back:
      cam.look_back();
      break;
    case ViewCubeFace::Left:
      cam.look_left();
      break;
    case ViewCubeFace::Right:
      cam.look_right();
      break;
    case ViewCubeFace::Top:
      cam.look_top();
      break;
    case ViewCubeFace::Bottom:
      cam.look_bottom();
      break;
  }
  request_redraw();
}

void ReplayViewport::on_view_cube_corner(ViewCubeCorner corner) {
  Vec3 dir{1.f, 1.f, 1.f};
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
  camera_.camera().look_toward(dir);
  request_redraw();
}

}  // namespace tamias
