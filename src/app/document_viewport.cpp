#include "document_viewport.h"

#include "core/log.h"

#include <QMouseEvent>
#include <QShowEvent>
#include <QResizeEvent>
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

DocumentViewport::DocumentViewport(std::shared_ptr<Document> document, QWidget* parent)
    : QWidget(parent), document_(std::move(document)) {
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_PaintOnScreen);
  setAttribute(Qt::WA_NoSystemBackground);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  camera_.frame_aabb(document_->bounds().valid() ? document_->bounds()
                                                 : Aabb{{-1, -1, -1}, {1, 1, 1}});
  rebuild_bvh();
}

DocumentViewport::~DocumentViewport() = default;

void DocumentViewport::set_render_mode(RenderMode mode) {
  mode_ = mode;
  request_redraw();
}

void DocumentViewport::frame_scene() {
  camera_.frame_aabb(document_->bounds());
  request_redraw();
}

void DocumentViewport::request_redraw() {
  update();
  submit_current_frame();
}

NativeWindowHandle DocumentViewport::native_handle() const {
  NativeWindowHandle handle{};
#if defined(_WIN32)
  handle.hwnd = reinterpret_cast<void*>(winId());
#else
  if (auto* ni = QGuiApplication::platformNativeInterface()) {
    handle.display = ni->nativeResourceForIntegration("display");
  }
  handle.window = static_cast<std::uint64_t>(winId());
#endif
  return handle;
}

void DocumentViewport::ensure_channel() {
  if (channel_) {
    return;
  }
  RenderDeviceConfig config{};
  config.backend = GraphicsBackend::Vulkan;
  config.enable_validation = true;
  render_thread_ = RenderThreadPool::instance().acquire(config);
  if (!render_thread_) {
    log_error("failed to acquire RenderThread");
    return;
  }
  channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
}

void DocumentViewport::rebuild_bvh() { bvh_.build(*document_); }

void DocumentViewport::submit_current_frame() {
  ensure_channel();
  if (!channel_) {
    return;
  }
  const auto dpr = devicePixelRatioF();
  const auto w = static_cast<std::uint32_t>((std::max)(1.0, width() * dpr));
  const auto h = static_cast<std::uint32_t>((std::max)(1.0, height() * dpr));
  channel_->resize(native_handle(), w, h);

  FrameSubmission frame{};
  frame.window = native_handle();
  frame.width = w;
  frame.height = h;
  const float aspect = static_cast<float>(w) / static_cast<float>(h);
  frame.view = camera_.view_matrix();
  frame.proj = camera_.proj_matrix(aspect);
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
  ensure_channel();
  request_redraw();
}

void DocumentViewport::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  request_redraw();
}

void DocumentViewport::paintEvent(QPaintEvent*) { submit_current_frame(); }

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
  if (orbiting_) {
    camera_.orbit(delta.x() * 0.01f, -delta.y() * 0.01f);
    request_redraw();
  } else if (panning_) {
    const float scale = camera_.distance() * 0.002f;
    camera_.pan(-delta.x() * scale, delta.y() * scale);
    request_redraw();
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
  camera_.dolly(std::pow(0.9f, steps));
  request_redraw();
}

}  // namespace tamias
