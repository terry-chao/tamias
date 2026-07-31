#pragma once

#include "core/native_window_handle.h"
#include "document/document.h"
#include "engine/render/render_runtime.h"
#include "math/camera.h"
#include "view/picking.h"

#include <QWidget>
#include <memory>

namespace tamias {

class DocumentViewport final : public QWidget {
  Q_OBJECT
 public:
  explicit DocumentViewport(std::shared_ptr<Document> document, QWidget* parent = nullptr);
  ~DocumentViewport() override;

  [[nodiscard]] Document& document() { return *document_; }
  void set_render_mode(RenderMode mode);
  void frame_scene();
  void request_redraw();

 protected:
  QPaintEngine* paintEngine() const override { return nullptr; }
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private:
  NativeWindowHandle native_handle() const;
  void ensure_channel();
  void submit_current_frame();
  void rebuild_bvh();

  std::shared_ptr<Document> document_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  TurntableCamera camera_;
  Bvh bvh_;
  RenderMode mode_ = RenderMode::Shaded;
  QPoint last_mouse_;
  QPoint press_mouse_;
  bool orbiting_ = false;
  bool panning_ = false;
};

}  // namespace tamias
