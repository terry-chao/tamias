#pragma once

#include "engine/base/native_window_handle.h"
#include "bim/drawing_import.h"
#include "bim/grid.h"
#include "app/drawing/drawing_document.h"
#include "engine/document/document.h"
#include "command/core/command_system.h"
#include "engine/render/resource/material.h"
#include "engine/render/resource/texture_asset.h"
#include "engine/render/runtime/render_runtime.h"
#include "engine/render/text/font_search.h"
#include "engine/render/text/glyph_atlas.h"
#include "engine/render/text/label_occluder.h"
#include "engine/render/text/stb_font.h"
#include "engine/render/text/text_align.h"
#include "engine/render/text/text_style.h"
#include "engine/render/text/text_kind_set.h"
#include "engine/document/document_io.h"
#include "engine/math/camera.h"
#include "engine/document/picking.h"
#include "app/viewport/overlay/box_select_overlay.h"
#include "app/viewport/overlay/view_cube_widget.h"
#include "app/viewport/canvas/armed_placement.h"
#include "app/viewport/canvas/viewport_floor.h"
#include "app/viewport/panel/viewport_tool_panel.h"
#include "entity/core/entity_grip.h"
#include "engine/modeling/feature/feature.h"
#include "host/session.h"
#include "plugin/plugin_point_input_session.h"

#include <QElapsedTimer>
#include <QLabel>
#include <QPoint>
#include <QTimer>
#include <QWidget>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace tamias {

// 底图贴图用的资产 id 起点：文档贴图 id 从 1 开始，这边用高位段隔开，
// 同一个渲染线程上两边的 id 不会撞（见 RenderThread::upload_texture）。
inline constexpr std::uint64_t kDrawingTextureAssetIdBase = 1ull << 56;
// 字形图集贴图的资产 id（和图纸底图、文档贴图都错开）。
inline constexpr std::uint64_t kTextAtlasTextureAssetId = 1ull << 57;

// 当前文档里各类构件的数量，供可见性面板显示 "墙 12" 这样的计数。
struct VisibilityCounts {
  std::unordered_map<EntityKind, std::size_t> kinds;
  std::size_t imported = 0;  // 无实体的导入网格（STEP / OBJ / glTF…）
};

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
  // X 光（X-Ray）：所有有面的构件画成半透明，能一眼看穿整栋楼。
  // 与显示模式（线框/着色/真实感）正交，可叠加；线框模式与线条图元不受影响。
  void set_xray(bool on);
  [[nodiscard]] bool xray() const { return xray_; }
  void frame_scene();
  // 框显指定句柄的构件（找不到或无有效包围盒则不动）。
  void frame_node(std::uint64_t node_id);
  void request_redraw();
  void set_plan_view(bool plan, bool restore_perspective = true);
  [[nodiscard]] bool plan_view() const { return plan_view_; }
  void hide_selected();
  void isolate_selected();
  void show_all_visible();
  // ==== 按构件类别过滤（构件可见性面板用；勾选 = 显示）====
  [[nodiscard]] const std::unordered_set<EntityKind>& hidden_kinds() const { return hidden_kinds_; }
  void set_kind_hidden(EntityKind kind, bool hidden);
  // 批量设置多个类别（面板整组勾选）：只重绘一次。
  void set_kinds_hidden(const std::vector<EntityKind>& kinds, bool hidden);
  // 只留这一类：其余类别与导入网格都隐藏；已隔离/单独隐藏的对象一并复位。
  void isolate_kind(EntityKind kind);
  // 相机框住该类全部构件。
  void frame_kind(EntityKind kind);
  [[nodiscard]] VisibilityCounts visibility_counts() const;
  [[nodiscard]] bool imported_hidden() const;
  void set_imported_hidden(bool hidden);
  // 隔离（右键"隔离选中"）状态：面板要据此把被挡住的类别显示成未勾选。
  [[nodiscard]] bool is_isolating() const { return !isolated_ids_.empty(); }
  [[nodiscard]] std::unordered_set<EntityKind> isolated_kinds() const;
  [[nodiscard]] bool has_active_filter() const;
  // ==== 按楼层显隐（楼层面板用；勾选 = 显示）====
  // 楼层带（标高 / 层高 / 夹层）只按**文档楼层表**重算：表里没有就没有楼层，
  // 不从几何猜（画个东西冒出一层是错的）。加层 / 删层走「楼层设置」。
  [[nodiscard]] std::vector<ViewportFloor> floors();
  [[nodiscard]] bool floor_hidden(std::size_t index) const;
  void set_floor_hidden(std::size_t index, bool hidden);
  void clear_floor_filter();
  [[nodiscard]] bool has_floor_filter() const { return !hidden_floors_.empty(); }
  // 开合视口右上角工具面板里的"构件显隐"页（Ribbon / 快捷键走这里）。
  void toggle_visibility_panel();
  // 开合同一列的"楼层"页（Ribbon / 快捷键走这里）。
  void toggle_floor_panel();
  // 开合同一列的"楼层管理"页（楼层视图清单）。
  void toggle_floor_manager_panel();
  // 开合同一列的"图纸管理"页（参考图纸清单）。
  void toggle_drawing_panel();
  // 直接开/关"图纸管理"页（刚挂上图纸时把它翻出来）。
  void set_drawing_panel_open(bool open);
  // 图纸管理页用：把图纸挂到当前文档 / 从清单里去掉（清单随 .tdoc 存）。
  void add_document_drawings(const std::vector<std::string>& paths);
  void remove_document_drawing(const std::string& path);
  // ==== 参考图纸底图（直接画在三维视口里）====
  // 底图总开关（Ribbon「图纸」）：关掉以后所有图纸都不画，单张的勾选状态照旧。
  void set_drawings_visible(bool visible);
  [[nodiscard]] bool drawings_visible() const { return drawings_visible_; }
  // 单张图纸的显隐（面板上那一列的勾）；写回文档并标脏。
  void set_drawing_visible(const std::string& path, bool visible);
  // 单张图纸的摆放 / 页号（「图纸设置」对话框的落点）；写回文档并标脏。
  void set_drawing_placement(const std::string& path, const DrawingPlacement& placement, int page);
  // 按模型范围重新摆一次（面板「对齐到模型」）。
  void fit_drawing_to_model(const std::string& path);
  // 相机框到某张图纸的平面上（面板「定位」）。
  void frame_drawing(const std::string& path);
  // 面板显示"读不出来 / 文件缺失"用；空 = 这张图纸读进来了。
  [[nodiscard]] QString drawing_status(const std::string& path) const;
  // 「图纸设置」对话框用：页数、图纸自带单位、按单位（有的话）/ 模型范围算出的摆放。
  struct DrawingInfo {
    int page_count = 1;
    double declared_unit_scale = 0.0;  // 0 = 图纸没写单位
    QString error;
  };
  // 没加载过的图纸会顺手加载一次（打开设置对话框时不该看到空页数）。
  [[nodiscard]] std::optional<DrawingInfo> drawing_info(const std::string& path);
  [[nodiscard]] std::optional<DrawingPlacement> suggested_drawing_placement(
      const std::string& path, int page);
  // ==== 楼层视图（楼层管理页用；默认打开的是全局三维）====
  // 当前打开的是不是"某一层的视图"；没打开时就是全局三维。
  [[nodiscard]] bool floor_view_open() const { return floor_view_.has_value(); }
  [[nodiscard]] std::size_t floor_view_index() const { return floor_view_.value_or(0); }
  // 打开全局三维：所有楼层可见 + 透视 + 框住整个模型。
  void open_global_view();
  // 打开某一层的视图：只显示该层、把它设为当前楼层、切到平面（2D）并框到这一层。
  // 打开的是"这一层"而不是"这一层的平面"：2D/3D 只是同一张视图的两种看法，
  // 切回三维（set_plan_view(false)）仍然是该层的三维，只留这一层的过滤照旧。
  void open_floor_view(std::size_t floor_index);
  [[nodiscard]] ViewportState capture_viewport_state() const;
  [[nodiscard]] RenderScene::View capture_render_scene_view() const;
  [[nodiscard]] std::vector<std::uint64_t> capture_hidden_node_ids() const;
  [[nodiscard]] RenderScene capture_debug_scene() const;
  void apply_viewport_state(const ViewportState& state);
  // 设置当前创建工具（None / Wall / 梁柱等；有规格的构件走绘制面板武装）。
  void set_tool(ToolMode mode);
  // 用绘制面板确认的参数武装一个创建命令（取消旧 pending，按 args dispatch）。
  void arm_create(ToolMode mode, const CommandArgs& args);
  [[nodiscard]] ToolMode tool_mode() const { return session_->tool_mode(); }
  // 撤销 / 重做最近一条命令。
  void undo();
  void redo();
  // 改实体某个特征参数（走 set_param 命令，可撤销；供属性面板调用）。
  void set_entity_param(std::uint64_t entity_id, std::uint64_t feature_id,
                        const std::string& param_name, double value);
  // 给实体分配/新建材质（走 set_material 命令，可撤销；供属性面板调用）。
  void set_entity_material(std::uint64_t entity_id, const Material& material);
  std::uint64_t import_texture(TextureAsset asset);
  void replace_texture(std::uint64_t id, TextureAsset asset);
  void update_library_material(const Material& material);
  void create_storey(const std::string& name, double elevation);
  void set_active_storey(std::uint64_t storey_id);
  // 楼层设置对话框的落点：整表替换楼层（可撤销）。
  void apply_storey_settings(std::vector<Storey> storeys, std::uint64_t active_storey_id);
  // 轴网设置对话框的落点：整表替换轴网（可撤销）。
  void apply_grid_settings(std::vector<GridAxis> axes);
  // 轴网放置：对话框确定后进入"布置 → 点一下落位"的一步放置。锚点是表里对应的
  // 基准点（生成行的原点），点击时整张轴网平移，让锚点落在点击处。Esc / 右键取消。
  void begin_grid_placement(std::vector<GridAxis> axes, Vec2 anchor);
  [[nodiscard]] bool grid_placement_active() const { return pending_grid_.has_value(); }
  void cancel_grid_placement();
  // 翻模对话框的落点：把复核后的候选一次落进文档（一条命令 = 一步撤销）。
  void apply_drawing_import(DrawingImportPlan plan);
  // 轴网显示开关（视图 → 轴网）；轴网是参考线，不进实体表。
  void set_grid_visible(bool visible);
  [[nodiscard]] bool grid_visible() const { return grid_visible_; }
  // ==== 文字标注（见 docs/TEXT.md §4.4）====
  // 按类别开关：轴号 / 标高 / 尺寸 / 房间名…。关掉的类别这一帧不排版、不画。
  [[nodiscard]] bool label_kind_visible(TextKind kind) const { return label_kinds_.visible(kind); }
  void set_label_kind_visible(TextKind kind, bool visible);
  // ==== 用户放的文字注记（见 docs/TEXT.md §5）====
  // 进入「点一下放文字」的一步放置（Esc / 右键取消）。
  void begin_text_placement();
  [[nodiscard]] bool text_placement_active() const { return text_placement_; }
  void cancel_text_placement();
  // 选中的注记（0 = 没选中）。属性面板 / 右键菜单据此显示。
  [[nodiscard]] std::uint64_t selected_text_id() const { return selected_text_id_; }
  void select_text(std::uint64_t text_id);
  // 就地改文字（双击 / 右键菜单都走它，一步撤销）。
  void edit_text_annotation(std::uint64_t text_id);
  void set_entity_location(std::uint64_t entity_id, std::uint64_t storey_id,
                           double elevation_offset);
  // 给选中实体追加倒圆角 / 倒斜角特征（走 fillet/chamfer 命令，可撤销）。
  void fillet_selected(double radius = 0.05);
  void chamfer_selected(double distance = 0.05);
  // 删除当前选中实体（走 delete_entity 命令，可撤销）。
  void delete_selected();
  // ==== 通用编辑：移动 / 复制 / 旋转 / 镜像 / 阵列 ====
  // 交互式：武装「点基点 → 点目标点」工具（旋转三点、镜像两点定轴），Esc / 右键取消。
  void begin_move_selection();
  void begin_copy_selection();
  void begin_rotate_selection();
  void begin_mirror_selection();
  // 一步到位（脚本 / 菜单带参数时用）。
  void move_selection(Vec3 delta);
  void copy_selection(Vec3 delta);
  void rotate_selection(Vec3 center, double angle_deg);
  // 阵列参数（对话框收齐后落到这里）。count 含原件，所以副本数是 count-1。
  struct ArrayParams {
    bool polar = false;
    int count = 3;
    double spacing = 1.0;      // 线性：相邻间距（米）
    double step_angle = 15.0;  // 环形：每份夹角（度）
    Vec3 direction{1.f, 0.f, 0.f};
    Vec3 center{};
  };
  void array_selection(const ArrayParams& params);
  [[nodiscard]] CommandSystem& command_system() { return command_system_; }
  // 会话层：文档 / 命令 / 相机 / 工具 / 选择都在这。
  [[nodiscard]] Session& session() { return *session_; }
  void refresh_after_edit();
  void notify_selection_changed();
  void set_debug_overlay(std::optional<Aabb> aabb, std::optional<std::uint64_t> isolate_node);
  void set_debug_vertex(std::optional<DebugVertexOverlay> vertex);
  Result<void> begin_plugin_point_input(
      PluginPointInputRequest request, PluginHost::PointInputCompletion completion);
  void cancel_plugin_point_input(std::uint64_t request_id = 0);

 signals:
  void tool_mode_changed(ToolMode mode);
  void selection_changed();  // 选中对象变化
  void document_changed();   // 文档内容/参数变化（undo/redo/命令执行后）
  void status_message(const QString& text);  // 状态栏提示（如三维中拒绝画板）
  // 命令回显：会话层执行完一条命令后给出等价 C# 调用（主窗口的控制台面板收）。
  void console_message(const QString& text);
  void plugin_point_input_changed(bool active);
  void visibility_changed();               // 隐藏/隔离/楼层过滤变化，面板据此刷新
  void view_changed();  // 打开的视图变了（全局三维 ↔ 某楼层），楼层管理页据此换高亮
  // 图纸管理页要求把某张图纸开成二维页签（主窗口接）。
  void drawing_open_requested(const QString& path);
  void drawings_changed();  // 底图显隐 / 摆放变了，图纸管理页据此刷新

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
  // 底图：按文档里的图纸清单加载 / 光栅化 / 上传贴图，并往帧里塞平面。
  void sync_drawing_underlays();
  void submit_drawing_overlays(FrameSubmission& frame);
  // 文字标注：字体懒加载 + 图集上传 + 轴网编号四边形（见 docs/TEXT.md §4.1 / §6 入口 1）。
  void ensure_text_font();
  void append_text_annotations(FrameSubmission& frame);
  void append_grid_axis_labels(FrameSubmission& frame, const Mat4& view_proj);
  void append_storey_labels(FrameSubmission& frame, const Mat4& view_proj);
  void append_grid_dimensions(FrameSubmission& frame, const Mat4& view_proj);
  void append_user_text_annotations(FrameSubmission& frame, const Mat4& view_proj);
  // 点一下放文字：问文本 → create_text 命令。
  void commit_text_placement(const QPoint& pos);
  // 屏幕命中的注记 id（0 = 没命中）。后放的压在上面，从后往前找。
  [[nodiscard]] std::uint64_t pick_text_annotation_at(const QPoint& pos) const;
  // 一条注记的屏幕矩形（Qt 逻辑像素，和鼠标事件坐标同一套）。
  [[nodiscard]] bool text_annotation_rect(const TextAnnotation& annotation, QRectF& out) const;
  // 一条标注的完整流程：投影锚点 → 挑字体 → 排版 → 去重叠 → 出四边形。
  // 返回 false = 没画（视口外 / 位置被更重要的标注占住）。
  bool append_label(FrameSubmission& frame, const Mat4& view_proj, Vec3 anchor_world,
                    const std::string& text, const TextStyle& style, TextAlign align,
                    float offset_x, float offset_y);
  [[nodiscard]] Aabb2 drawing_footprint() const;
  // 新挂上的图纸摆在哪：图纸写了单位就按单位换算，否则按模型 / 轴网范围适配。
  [[nodiscard]] DrawingPlacement default_drawing_placement_for(const std::string& path);
  void layout_overlays();
  void sync_view_cube();
  void sync_coord_readout();
  void start_view_animation(float target_yaw, float target_pitch,
                           bool finish_orthographic = false);
  void stop_view_animation();
  // 平面 / 三维切换的实现体：animate=false 时立刻到位（打开楼层视图要一次落到
  // "该层平面"，不能先转一半再被 framing 打断）。三维不吃掉楼层视图状态：在某一
  // 层的视图里切回三维 = 该层的三维（见 open_floor_view）。
  void apply_plan_view(bool plan, bool restore_perspective, bool animate);
  void refresh_floors();
  // 某一层视图的包围盒：模型的平面范围 + 这层的标高区间（平面视图下高度不参与
  // 投影，压到这一层只是为了框住这一层的平面范围）；没有楼层表时返回无效盒。
  [[nodiscard]] Aabb floor_view_box(std::size_t floor_index) const;
  [[nodiscard]] bool node_visible_in_view(std::uint64_t id) const;
  // 导入网格（无 Entity 的 SceneNode）的节点 id。
  [[nodiscard]] std::vector<std::uint64_t> imported_node_ids() const;
  [[nodiscard]] Vec3 cursor_world_position(const QPoint& pos) const;
  [[nodiscard]] Vec3 cursor_ground_position(const QPoint& pos) const;
  // 轴网放置用：射线与当前楼层标高求交。轴网预览画在那个高度上，透视下才点得准。
  [[nodiscard]] Vec3 plan_position_at_storey(const QPoint& pos) const;
  // 绘制实体时吸附到地面网格交点（门/窗贴墙拾取除外）。
  [[nodiscard]] bool grid_snap_active() const;
  [[nodiscard]] std::uint64_t pick_node_at(const QPoint& pos) const;
  // 轴网显示所在标高（数据恒在 y = 0，画/点都抬到当前楼层）。
  [[nodiscard]] float grid_plane_y() const;
  // 点选轴线（屏幕距离，容差见 kGridPickPixels）；没命中返回 0。
  [[nodiscard]] std::uint64_t pick_grid_axis_at(const QPoint& pos) const;
  void select_grid_axis(std::uint64_t axis_id, bool additive);
  void show_grid_context_menu(const QPoint& global_pos);
  // 布置门窗：沿视线找最近的墙（忽略楼板等遮挡）。
  [[nodiscard]] std::optional<std::pair<std::uint64_t, Vec3>> pick_wall_at(const QPoint& pos) const;
  [[nodiscard]] bool is_opening_placement_tool() const;
  void update_opening_hover(const QPoint& pos);
  void show_entity_context_menu(const QPoint& global_pos);
  void adjust_selected_param(double delta);
  void run_command(const std::string& name, const CommandArgs& args, bool notify = true);
  // 会新增实体的编辑（复制 / 阵列）：跑完把选择换成新建的副本（CAD 惯例）。
  void run_creating_command(const std::string& name, const CommandArgs& args);
  // 武装一个交互式变换工具：命令名 + 提示语（见 command/edit/transform_tool_command.h）。
  void begin_transform_tool(const std::string& command, const QString& hint);
  // 跑完复制类工具后，用前后 id 差集把选择换成新建的副本。
  void select_entities_created_since(const std::vector<std::uint64_t>& before_ids);
  void dispatch_tool_command(ToolMode mode);
  // 武装一个构件命令（dispatch）并记下这一刻的楼层放置状态。
  void dispatch_armed_component(const std::string& command, const CommandArgs& args);
  // 用面板最近一次武装的参数重新武装当前工具（连续绘制同类型构件时用）。
  void rearm_tool();
  // 当前楼层（或它的标高 / 层高）变了以后，武装中的构件命令要按新楼层重画一遍：
  // 不然命令还拿着旧楼层的标高执行，归属却写成新楼层（见 armed_placement.h）。
  void sync_armed_placement();
  void resync_all_meshes();
  void resync_textures();
  void cancel_tool();
  // 丢弃放置会话（不重绘、不提示）；落位 / 取消 / 被别的工具顶掉都走它。
  void clear_grid_placement();
  void commit_grid_placement(const QPoint& pos);
  // 放置预览用的表：整张轴网按光标位置平移后的副本。
  [[nodiscard]] std::vector<GridAxis> ghost_grid_axes(const QPoint& pos) const;
  void refuse_slab_outside_plan(bool popup);
  [[nodiscard]] bool finish_pending_if_done(const Result<bool>& done);
  [[nodiscard]] Vec3 snapped_ground_position(const QPoint& pos) const;
  // 相机只渲染三维区域（surface_，右侧工具列不参与渲染）。鼠标事件由三维子窗口
  // 转发上来，坐标就是它的局部坐标；射线与投影必须按三维区域的宽高（而不是整块
  // 视口的宽高）算，否则光标越靠右、算出的世界点越偏——画的墙就不在鼠标下起笔。
  [[nodiscard]] QSize scene_area_size() const;
  [[nodiscard]] Ray ray_at(const QPoint& pos) const;
  void update_box_select_rect(const QPoint& pos);
  void finish_box_select(const QPoint& pos, bool additive);
  [[nodiscard]] Mat4 view_proj() const;
  [[nodiscard]] bool pick_grip_at(const QPoint& pos, EntityGrip& out) const;
  void apply_grip_at(const QPoint& pos);
  void commit_grip_drag();
  void clear_grip_preview();
  void fill_grip_overlay(FrameSubmission& frame) const;
  void fill_debug_overlay(FrameSubmission& frame) const;
  std::unique_ptr<Session> session_;
  PluginPointInputSession plugin_point_input_;
  Document* document_ = nullptr;
  CommandSystem& command_system_;
  TurntableCamera& camera_;
  std::shared_ptr<RenderThread> render_thread_;
  std::unique_ptr<RenderChannel> channel_;
  Bvh bvh_;
  RenderMode mode_ = RenderMode::Shaded;
  bool xray_ = false;
  class NativeSurface;
  NativeSurface* surface_ = nullptr;
  void* gl_hwnd_ = nullptr;  // Win32 OpenGL child HWND (UI-thread owned)
  ViewCubeWidget* view_cube_ = nullptr;
  ViewportToolPanel* tool_panel_ = nullptr;
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
  bool plugin_input_press_ = false;
  // 这一下左键已经被轴网消费掉（落位 / 选中轴线），抬起时别再当选择点击处理。
  bool grid_press_consumed_ = false;
  bool gripping_ = false;
  // 复制类交互工具武装时记下的实体清单：跑完用差集把选择移到副本上。
  std::vector<std::uint64_t> created_watch_before_;
  EntityGrip active_grip_{};
  FeatureModel grip_from_model_{};
  Mat4 grip_from_transform_ = Mat4::identity();
  Vec3 grip_from_world_{};
  FeatureModel grip_to_model_{};
  Mat4 grip_to_transform_ = Mat4::identity();
  std::vector<Vec3> grip_preview_polyline_;
  std::vector<Vec3> grip_preview_points_;
  bool grip_preview_valid_ = false;
  BoxSelectOverlay* box_select_overlay_ = nullptr;
  bool alive_ = true;
  bool has_cursor_ = false;
  std::unordered_map<std::uint64_t, std::uint64_t> uploaded_textures_;  // asset id -> generation
  // 参考图纸底图：文档里只有路径 + 摆放，加载出来的图纸与 GPU 贴图缓存在这。
  struct DrawingUnderlay {
    std::unique_ptr<DrawingDocument> document;
    QString error;                        // 非空 = 这张图纸读不出来
    std::uint64_t texture_asset_id = 0;   // upload_texture 用的保留 id
    std::uint64_t texture_id = 0;         // 上传成功后的 GPU 贴图 id
    std::uint64_t texture_generation = 0;
    int raster_page = -1;
    int raster_edge = 0;
    bool needs_upload = false;
  };
  std::unordered_map<std::string, DrawingUnderlay> drawing_underlays_;
  std::uint64_t next_drawing_texture_asset_id_ = kDrawingTextureAssetIdBase;
  bool drawings_visible_ = true;
  // 屏幕空间文字：字体（主字体 + 中文回落）+ 字形图集 + 图集贴图。
  // 找不到字体就整条通路安静关掉。
  std::vector<std::shared_ptr<StbFont>> label_fonts_;
  bool text_font_loaded_ = false;
  std::unique_ptr<GlyphAtlas> glyph_atlas_;
  TextKindSet label_kinds_;
  // 轴号 / 标高 / 尺寸共用一张占用表：标注之间不互相压住。
  LabelOccluder label_occluder_{3.f};
  std::uint64_t text_atlas_texture_id_ = 0;
  std::uint64_t text_atlas_generation_ = 0;
  std::uint64_t text_tick_ = 0;
  bool text_placement_ = false;
  std::uint64_t selected_text_id_ = 0;
  // 这一下左键已经被文字注记消费掉（选中），抬起时别再当空白点击清选择。
  bool text_press_consumed_ = false;
  std::unordered_set<std::uint64_t> hidden_ids_;
  std::unordered_set<std::uint64_t> isolated_ids_;
  std::unordered_set<EntityKind> hidden_kinds_;
  std::vector<ViewportFloor> floors_;
  std::unordered_set<int> hidden_floors_;  // 空 = 全部楼层可见
  std::uint64_t last_submitted_scene_generation_ = 0;  // 脏标记游标（见 Scene::dirty_since）
  bool plan_view_ = false;
  // 打开的楼层视图（floors_ 的下标）；空 = 默认的全局三维。
  std::optional<std::size_t> floor_view_;
  bool grid_visible_ = true;
  // 放置中的轴网：整张表 + 锚点。只在"轴网设置 → 确定"到落位之间非空。
  struct GridPlacement {
    std::vector<GridAxis> axes;
    Vec2 anchor;
  };
  std::optional<GridPlacement> pending_grid_;
  // 绘制面板最近一次武装的参数（连续绘制同类型构件时复用，避免回退到硬编码默认）。
  ToolMode last_arm_mode_ = ToolMode::None;
  CommandArgs last_arm_args_;
  // 武装那一刻的楼层放置状态；楼层一变就重新武装（见 sync_armed_placement）。
  ArmedPlacement armed_placement_;
  float persp_yaw_ = 0.785398163f;
  float persp_pitch_ = 0.35f;
  std::optional<Aabb> debug_aabb_;
  std::optional<std::uint64_t> debug_isolate_node_;
  std::optional<DebugVertexOverlay> debug_vertex_;
};

}  // namespace tamias
