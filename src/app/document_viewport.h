#pragma once

#include "engine/core/native_window_handle.h"
#include "engine/document/document.h"
#include "command/command_system.h"
#include "engine/render/render_runtime.h"
#include "engine/document/document_io.h"
#include "engine/math/camera.h"
#include "engine/document/picking.h"
#include "box_select_overlay.h"
#include "view_cube_widget.h"
#include "viewport_floor.h"
#include "viewport_tool_strip.h"
#include "entity/entity_grip.h"
#include "engine/modeling/feature.h"
#include "host/session.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace tamias {

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
  void set_plan_view(bool plan, bool restore_perspective = true);
  [[nodiscard]] bool plan_view() const { return plan_view_; }
  void hide_selected();
  void isolate_selected();
  void show_all_visible();
  [[nodiscard]] ViewportState capture_viewport_state() const;
  void apply_viewport_state(const ViewportState& state);
  // 设置当前创建工具（None / Wall / Box / Cylinder）。
  void set_tool(ToolMode mode);
  [[nodiscard]] ToolMode tool_mode() const { return session_->tool_mode(); }
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
  // 删除当前选中实体（走 delete_entity 命令，可撤销）。
  void delete_selected();
  [[nodiscard]] CommandSystem& command_system() { return command_system_; }
  // 会话层：文档 / 命令 / 相机 / 工具 / 选择都在这。
  [[nodiscard]] Session& session() { return *session_; }
  void refresh_after_edit();

 signals:
  void tool_mode_changed(ToolMode mode);
  void selection_changed();  // 选中对象变化
  void document_changed();   // 文档内容/参数变化（undo/redo/命令执行后）
  void status_message(const QString& text);  // 状态栏提示（如三维中拒绝画板）

 protected:
  void showEvent(QShowEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
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
  void start_view_animation(float target_yaw, float target_pitch,
                           bool finish_orthographic = false);
  void stop_view_animation();
  void populate_visibility_menu();
  void populate_floor_menu();
  void refresh_floors();
  void set_active_floor(int index);
  void set_kind_hidden(EntityKind kind, bool hidden);
  [[nodiscard]] bool node_visible_in_view(std::uint64_t id) const;
  [[nodiscard]] Vec3 cursor_world_position(const QPoint& pos) const;
  [[nodiscard]] Vec3 cursor_ground_position(const QPoint& pos) const;
  // 绘制实体时吸附到地面网格交点（门/窗贴墙拾取除外）。
  [[nodiscard]] bool grid_snap_active() const;
  [[nodiscard]] std::uint64_t pick_node_at(const QPoint& pos) const;
  void show_entity_context_menu(const QPoint& global_pos);
  void adjust_selected_param(double delta);
  void run_command(const std::string& name, const CommandArgs& args, bool notify = true);
  void dispatch_tool_command(ToolMode mode);
  void resync_all_meshes();
  void resync_textures();
  void cancel_tool();
  void refuse_slab_outside_plan(bool popup);
  [[nodiscard]] bool finish_pending_if_done(const Result<bool>& done);
  [[nodiscard]] Vec3 snapped_ground_position(const QPoint& pos) const;
  void update_box_select_rect(const QPoint& pos);
  void finish_box_select(const QPoint& pos, bool additive);
  [[nodiscard]] Mat4 view_proj() const;
  [[nodiscard]] bool pick_grip_at(const QPoint& pos, EntityGrip& out) const;
  void apply_grip_at(const QPoint& pos);
  void commit_grip_drag();
  void fill_grip_overlay(FrameSubmission& frame) const;

  std::unique_ptr<Session> session_;
  Document* document_ = nullptr;
  CommandSystem& command_system_;
  TurntableCamera& camera_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  Bvh bvh_;
  RenderMode mode_ = RenderMode::Shaded;
  class NativeSurface;
  NativeSurface* surface_ = nullptr;
  void* gl_hwnd_ = nullptr;  // Win32 OpenGL child HWND (UI-thread owned)
  ViewCubeWidget* view_cube_ = nullptr;
  ViewportToolStrip* tool_strip_ = nullptr;
  QLabel* coord_label_ = nullptr;
  QTimer* view_anim_timer_ = nullptr;
  QElapsedTimer view_anim_clock_;
  float anim_from_yaw_ = 0.f;
  float anim_from_pitch_ = 0.f;
  float anim_yaw_delta_ = 0.f;
  float anim_to_yaw_ = 0.f;
  float anim_to_pitch_ = 0.f;
  bool anim_finish_orthographic_ = false;
  QPoint last_mouse_;
  QPoint press_mouse_;
  std::uint64_t press_hit_ = 0;
  bool panning_ = false;
  bool mmb_nav_ = false;
  bool box_selecting_ = false;
  bool gripping_ = false;
  EntityGrip active_grip_{};
  FeatureModel grip_from_model_{};
  Mat4 grip_from_transform_ = Mat4::identity();
  Vec3 grip_from_world_{};
  BoxSelectOverlay* box_select_overlay_ = nullptr;
  bool alive_ = true;
  bool has_cursor_ = false;
  std::unordered_set<std::uint64_t> uploaded_textures_;  // 已上传过的纹理资产 id
  std::unordered_set<std::uint64_t> hidden_ids_;
  std::unordered_set<std::uint64_t> isolated_ids_;
  std::unordered_set<EntityKind> hidden_kinds_;
  std::vector<ViewportFloor> floors_;
  int active_floor_ = -1;  // -1 = all floors
  bool plan_view_ = false;
  float persp_yaw_ = 0.785398163f;
  float persp_pitch_ = 0.35f;
};

}  // namespace tamias
