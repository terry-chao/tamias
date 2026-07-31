#include "document_viewport.h"

#include "app_settings.h"
#include "core/log.h"

#include <QCoreApplication>
#include <QMouseEvent>
#include <QShowEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

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

DocumentViewport::DocumentViewport(std::shared_ptr<Document> document, QWidget* parent)
    : QWidget(parent), document_(std::move(document)) {
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  // Match Vulkan clear so any uncovered edge never flashes pure black.
  setAutoFillBackground(true);
  QPalette pal = palette();
  pal.setColor(QPalette::Window, QColor(31, 33, 38));
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
  connect(view_cube_, &ViewCubeWidget::orbit_dragged, this, [this](float dyaw, float dpitch) {
    camera_.orbit(dyaw, dpitch);
    request_redraw();
  });

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
}

void DocumentViewport::set_render_mode(RenderMode mode) {
  mode_ = mode;
  request_redraw();
}

void DocumentViewport::frame_scene() {
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

void DocumentViewport::on_view_cube_face(ViewCubeFace face) {
  switch (face) {
    case ViewCubeFace::Front:
      camera_.look_front();
      break;
    case ViewCubeFace::Back:
      camera_.look_back();
      break;
    case ViewCubeFace::Left:
      camera_.look_left();
      break;
    case ViewCubeFace::Right:
      camera_.look_right();
      break;
    case ViewCubeFace::Top:
      camera_.look_top();
      break;
    case ViewCubeFace::Bottom:
      camera_.look_bottom();
      break;
  }
  request_redraw();
}

NativeWindowHandle DocumentViewport::native_handle() const {
  NativeWindowHandle handle{};
#if defined(_WIN32)
  handle.hwnd = reinterpret_cast<void*>(surface_ ? surface_->winId() : winId());
#else
  if (auto* ni = QGuiApplication::platformNativeInterface()) {
    handle.display = ni->nativeResourceForIntegration("display");
  }
  handle.window = static_cast<std::uint64_t>(surface_ ? surface_->winId() : winId());
#endif
  return handle;
}

void DocumentViewport::ensure_channel() {
  if (!alive_ || channel_) {
    return;
  }
  const RenderDeviceConfig config = AppSettings::instance().render_device_config();
  render_thread_ = RenderThreadPool::instance().acquire(config);
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
  const auto dpr = devicePixelRatioF();
  const auto w = static_cast<std::uint32_t>(surface_->width() * dpr);
  const auto h = static_cast<std::uint32_t>(surface_->height() * dpr);
  channel_->resize(native_handle(), w, h);

  FrameSubmission frame{};
  frame.window = native_handle();
  frame.width = w;
  frame.height = h;
  const float aspect = static_cast<float>(w) / static_cast<float>(h);
  frame.view = camera_.view_matrix();
  frame.proj = camera_.proj_matrix(aspect);
  frame.eye_position = camera_.eye_position();
  frame.mode = mode_;
  for (const auto& node : document_->scene().nodes()) {
    SceneDrawItem item{};
    item.node_id = node.id;
    item.mesh_id = node.gpu_mesh_id;
    item.transform = node.transform;
    item.color = node.color;
    item.selected = node.selected;
    frame.items.push_back(item);
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
  last_mouse_ = event->pos();
  press_mouse_ = event->pos();
  if (event->button() == Qt::LeftButton) {
    orbiting_ = true;
  } else if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
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
  } else {
    sync_coord_readout();
  }
}

void DocumentViewport::mouseReleaseEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    orbiting_ = false;
    if ((event->pos() - press_mouse_).manhattanLength() < 4) {
      const auto dpr = devicePixelRatioF();
      const float aspect = static_cast<float>((std::max)(1, width())) /
                           static_cast<float>((std::max)(1, height()));
      const Ray ray =
          camera_ray(camera_, aspect, static_cast<float>(event->pos().x() * dpr),
                     static_cast<float>(event->pos().y() * dpr),
                     static_cast<float>(width() * dpr), static_cast<float>(height() * dpr));
      document_->scene().clear_selection();
      if (auto hit = bvh_.closest_hit(ray, *document_)) {
        if (auto* node = document_->scene().find(hit->node_id)) {
          node->selected = true;
        }
      }
      request_redraw();
    }
  }
  if (event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
    panning_ = false;
  }
}

void DocumentViewport::wheelEvent(QWheelEvent* event) {
  const float steps = event->angleDelta().y() / 120.f;
  const float factor = std::pow(0.9f, steps);
  const QPoint pos = event->position().toPoint();
  last_mouse_ = pos;
  has_cursor_ = true;

  // Zoom toward the cursor: dolly, then shift the orbit target so the world
  // point under the mouse stays fixed in screen space.
  const Vec3 focus = cursor_world_position(pos);
  const Vec3 old_target = camera_.target();
  camera_.dolly(factor);
  camera_.set_target(focus + (old_target - focus) * factor);
  request_redraw();
}

}  // namespace tamias
