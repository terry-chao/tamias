#pragma once

#include "core/native_window_handle.h"
#include "document/document.h"
#include "document/history.h"
#include "engine/render/render_runtime.h"
#include "io/document_io.h"
#include "math/camera.h"
#include "view/picking.h"
#include "view_cube_widget.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QWidget>
#include <memory>

namespace tamias {

class DocumentViewport final : public QWidget {
  Q_OBJECT
 public:
  explicit DocumentViewport(std::shared_ptr<Document> document,
                            std::shared_ptr<RenderThread> render_thread,
                            QWidget* parent = nullptr);
  ~DocumentViewport() override;

  [[nodiscard]] Document& document() { return *document_; }
  [[nodiscard]] DocumentHistory& history() { return history_; }
  [[nodiscard]] const DocumentHistory& history() const { return history_; }
  void set_render_mode(RenderMode mode);
  [[nodiscard]] RenderMode render_mode() const { return mode_; }
  void frame_scene();
  void request_redraw();
  [[nodiscard]] ViewportState capture_viewport_state() const;
  void apply_viewport_state(const ViewportState& state);
  // Seed undo baseline from the current document body (call after load/open).
  void seed_history_baseline();

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;

 private slots:
  void on_view_cube_face(ViewCubeFace face);
  void on_view_cube_corner(ViewCubeCorner corner);
  void on_view_anim_tick();

 private:
  NativeWindowHandle native_handle() const;
  void ensure_channel();
  void ensure_gl_surface();
  void destroy_gl_surface();
  void submit_current_frame();
  void rebuild_bvh();
  void layout_overlays();
  void sync_view_cube();
  void sync_coord_readout();
  void start_view_animation(float target_yaw, float target_pitch);
  void stop_view_animation();
  [[nodiscard]] Vec3 cursor_world_position(const QPoint& pos) const;

  std::shared_ptr<Document> document_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  DocumentHistory history_;
  TurntableCamera camera_;
  Bvh bvh_;
  RenderMode mode_ = RenderMode::Shaded;
  class NativeSurface;
  NativeSurface* surface_ = nullptr;
  void* gl_hwnd_ = nullptr;  // Win32 OpenGL child HWND (UI-thread owned)
  ViewCubeWidget* view_cube_ = nullptr;
  QLabel* coord_label_ = nullptr;
  QTimer* view_anim_timer_ = nullptr;
  QElapsedTimer view_anim_clock_;
  float anim_from_yaw_ = 0.f;
  float anim_from_pitch_ = 0.f;
  float anim_yaw_delta_ = 0.f;
  float anim_to_yaw_ = 0.f;
  float anim_to_pitch_ = 0.f;
  QPoint last_mouse_;
  QPoint press_mouse_;
  bool orbiting_ = false;
  bool panning_ = false;
  bool alive_ = true;
  bool has_cursor_ = false;
};

}  // namespace tamias
