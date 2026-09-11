#pragma once

#include "engine/math/camera.h"
#include "engine/render/render_runtime.h"
#include "engine/render/scene_debug_player.h"
#include "host/camera_controller.h"
#include "view_cube_widget.h"

#include <QWidget>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>

class QResizeEvent;
class QShowEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;

namespace tamias {

class ReplayViewport final : public QWidget {
  Q_OBJECT
 public:
  explicit ReplayViewport(std::shared_ptr<RenderThread> render_thread, QWidget* parent = nullptr);
  ~ReplayViewport() override;

  [[nodiscard]] SceneDebugPlayer& player() { return player_; }
  [[nodiscard]] const SceneDebugPlayer& player() const { return player_; }
  [[nodiscard]] CameraController& camera() { return camera_; }
  [[nodiscard]] RenderMode render_mode() const { return mode_; }

  void set_scene(RenderScene scene);
  void set_render_mode(RenderMode mode);
  void frame_scene();
  void request_redraw();
  void sync_from_player();

 signals:
  void node_picked(quint64 node_id);
  void status_message(const QString& text);

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

 private:
  class NativeSurface;

  NativeWindowHandle native_handle() const;
  void ensure_channel();
  void ensure_gl_surface();
  void destroy_gl_surface();
  void submit_current_frame();
  void layout_overlays();
  void sync_view_cube();
  void upload_resources();
  [[nodiscard]] std::uint64_t pick_node_at(const QPoint& pos) const;
  void on_view_cube_face(ViewCubeFace face);
  void on_view_cube_corner(ViewCubeCorner corner);

  SceneDebugPlayer player_;
  CameraController camera_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  RenderMode mode_ = RenderMode::Shaded;
  NativeSurface* surface_ = nullptr;
  void* gl_hwnd_ = nullptr;
  ViewCubeWidget* view_cube_ = nullptr;
  bool alive_ = true;
  QPoint last_mouse_;
  bool panning_ = false;
  bool mmb_nav_ = false;
  std::unordered_map<std::uint64_t, std::uint64_t> uploaded_textures_;
  std::unordered_set<std::uint64_t> uploaded_meshes_;
};

}  // namespace tamias
