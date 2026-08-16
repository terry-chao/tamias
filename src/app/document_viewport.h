#pragma once

#include "engine/core/native_window_handle.h"
#include "engine/document/document.h"
#include "command/command_system.h"
#include "engine/render/render_runtime.h"
#include "engine/document/document_io.h"
#include "engine/math/camera.h"
#include "engine/document/picking.h"
#include "view_cube_widget.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QTimer>
#include <QWidget>
#include <cstdint>
#include <memory>
#include <string>

namespace tamias {

// 当前激活的创建工具。
enum class ToolMode { None, Wall, Box, Cylinder };

class DocumentViewport final : public QWidget {
  Q_OBJECT
 public:
  explicit DocumentViewport(std::shared_ptr<Document> document,
                            std::shared_ptr<RenderThread> render_thread,
                            QWidget* parent = nullptr);
  ~DocumentViewport() override;

  [[nodiscard]] Document& document() { return *document_; }
  void set_render_mode(RenderMode mode);
  [[nodiscard]] RenderMode render_mode() const { return mode_; }
  void frame_scene();
  void request_redraw();
  [[nodiscard]] ViewportState capture_viewport_state() const;
  void apply_viewport_state(const ViewportState& state);
  // 设置当前创建工具（None / Wall / Box / Cylinder）。
  void set_tool(ToolMode mode);
  [[nodiscard]] ToolMode tool_mode() const { return tool_mode_; }
  // 撤销 / 重做最近一条命令。
  void undo();
  void redo();
  // 改实体某个特征参数（走 set_param 命令，可撤销；供属性面板调用）。
  void set_entity_param(std::uint64_t entity_id, std::uint64_t feature_id,
                        const std::string& param_name, double value);
  // 给实体分配/新建材质（走 set_material 命令，可撤销；供属性面板调用）。
  void set_entity_material(std::uint64_t entity_id, const Material& material);
  // 给选中实体追加倒圆角 / 倒斜角特征（走 fillet/chamfer 命令，可撤销）。
  void fillet_selected(double radius = 0.05);
  void chamfer_selected(double distance = 0.05);

 signals:
  void tool_mode_changed(ToolMode mode);
  void selection_changed();  // 选中对象变化
  void document_changed();   // 文档内容/参数变化（undo/redo/命令执行后）

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

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
  [[nodiscard]] Vec3 cursor_ground_position(const QPoint& pos) const;
  void adjust_selected_param(double delta);
  void run_command(const std::string& name, const CommandArgs& args, bool notify = true);
  void dispatch_tool_command(ToolMode mode);
  void resync_all_meshes();
  void cancel_tool();

  std::shared_ptr<Document> document_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  TurntableCamera camera_;
  Bvh bvh_;
  RenderMode mode_ = RenderMode::Shaded;
  CommandSystem command_system_{command_registry()};
  ToolMode tool_mode_ = ToolMode::None;
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
