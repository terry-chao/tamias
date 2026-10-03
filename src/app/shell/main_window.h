#pragma once

#include "engine/document/document.h"
#include "app/viewport/canvas/document_viewport.h"
#include "engine/render/runtime/render_runtime.h"
#include "app/shell/home_page.h"
#include "engine/document/document_io.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"
#include "app/base/recent_files.h"
#include "app/mcp/mcp_policy.h"

#include <QMainWindow>
#include <QString>
#include <QStringList>
#include <QStackedWidget>
#include <QTabWidget>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class QAction;
class QActionGroup;
class QCloseEvent;
class QDockWidget;
class QEvent;
class QMenu;
class QMenuBar;
class QToolButton;
class QTimer;

namespace tamias {

class PropertyPanel;
class HandleInspector;
class SceneDebuggerWindow;
class TextureLibraryPanel;
class TimingPanel;
class AiPanel;
class ConsolePanel;
class PluginManager;
class RibbonBar;
class RibbonGroup;
class DrawPanel;
class DrawingView;
class ExtensionWatcher;
class McpService;

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void open_paths(const QStringList& paths);
  // 开启 / 关闭内置 MCP 服务（loopback HTTP）。port = 0 时自动选端口。
  // 命令行 --mcp 在 main 里调它；服务跟着当前活动文档重新绑定。
  void set_mcp_enabled(bool enabled, quint16 port = 0, bool allow_evaluate = false,
                       McpPolicy policy = McpPolicy::kReadOnly);
  // 打开 AI 面板并把这句话发出去（--ai-prompt）。等文档打开后再发。
  void ask_ai(const QString& prompt);

 private slots:
  void open_file();
  bool save_file();
  bool save_file_as();
  void frame_all();
  void close_tab(int index);
  // 楼层面板点了一行：0 = 回文档页签（全局三维），其余 = 开 / 切那一层的独立页签。
  void on_floor_view_requested(DocumentViewport* source, std::uint64_t storey_id);
  void open_recent_path(const QString& path);
  void on_missing_recent(const QString& path);
  void open_drawing_file();
  void open_settings();
  void open_about();
  void open_graphics_diagnostics();
  void open_plugin_manager();
  void show_home();
  void show_documents();
  void activate_open_document(int index);
  void refresh_property_panel();
  void refresh_handle_inspector();
  void refresh_texture_library_panel();
  void sync_draw_panel();
  // 当前楼层（或它的层高）变了以后，把绘制面板上"跟着楼层走"的默认值刷新掉。
  void refresh_draw_panel_storey_defaults();

 private:
  void showEvent(QShowEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  // 面板停靠布局变了（拖动 / 改大小 / 显隐）会连发 LayoutRequest，节流后存盘。
  bool eventFilter(QObject* watched, QEvent* event) override;
  void persist_window_state();

  // Returns false if the user cancelled (or save failed) and the document
  // must stay open.
  bool confirm_close_document(DocumentViewport* vp);
  void activate_viewport(DocumentViewport* vp);
  using UiLoadProgressCallback = std::function<void(int, const QString&)>;
  void add_document_tab(std::shared_ptr<Document> document,
                        const ViewportState* viewport = nullptr,
                        const UiLoadProgressCallback& progress = {});
  // 造一张文档视口（含渲染线程 / 网格上传 / 与主窗口的全部连线），但不进页签。
  // 文档页签与楼层页签共用它——两者共享同一份 Document，只是视图状态各异。
  DocumentViewport* create_document_viewport(std::shared_ptr<Document> document,
                                             const UiLoadProgressCallback& progress = {});
  // 楼层页签：这份文档里那一层的独立视图（只显示这一层，本层底面当原点）。
  void open_floor_tab(DocumentViewport* source, std::uint64_t storey_id);
  // 关文档页签时把它的楼层页签一并关掉（楼层页签只是这份文档的几个房间）。
  void close_floor_tabs_for(Document* document);
  int find_floor_tab(const Document* document, std::uint64_t storey_id) const;
  int find_global_tab(const Document* document) const;
  Result<void> populate_document_meshes(Document& document, RenderThread& thread,
                                        const UiLoadProgressCallback& progress = {});
  void refresh_home();
  // ==== 菜单栏（FreeCAD 的排法：文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助）====
  // 菜单项和功能区共用同一批 QAction：改名、换图标、灰掉都只做一次。
  void build_menu_bar();
  // 「最近打开」是动态子菜单：每次弹出前按当前的记录重建。
  void refresh_recent_menu();
  // 「窗口」列出所有打开的文档（当前那个打勾），同样每次弹出前重建。
  void refresh_window_menu();
  // 没有打开文档时，把只对文档有意义的菜单项 / 工具按钮一起灰掉。
  void sync_document_actions();
  bool open_path(const QString& path);
  void open_drawing_tab(const QString& path);
  // 把图纸挂到有文档的视口里（视口里当底图看）；没有打开的文档视口时返回 false，
  // 调用方退回二维页签。已挂过的图纸直接定位过去。
  bool open_drawing_in_viewport(const QString& path);
  // 底下能挂图纸的文档视口：优先当前页，其次任意一个已打开的文档页。
  DocumentViewport* drawing_target_viewport() const;
  void new_document();
  void set_create_tool(ToolMode mode);
  void sync_create_tool_actions(ToolMode mode);
  bool write_selected_mesh(const QString& path);
  bool write_tdoc_document(const QString& path);
  bool write_render_scene_document(const QString& path, bool show_inspect);
  bool export_render_scene();
  bool pin_render_scene_golden();
  void debug_current_frame();
  void ensure_scene_debugger();
  void open_scene_debugger(RenderScene scene, const std::filesystem::path& path = {},
                           DocumentViewport* source = nullptr);
  void notify_save_success(const QString& path);
  void set_render_mode(RenderMode mode);
  void sync_render_mode_actions();
  void sync_bim_actions();
  void bind_plugin_session();
  // 按当前插件命令表重建 Ribbon 上的插件按钮（启动时 + 扩展重载之后）。
  // 显式收 RibbonBar：不依赖成员初始化顺序，也少一处跨模块耦合。
  void rebuild_plugin_ribbon(RibbonBar* ribbon);
  void apply_plugin_visibility();
  void apply_plugin_order();
  const MeshCpu* selected_mesh(Document& document) const;
  const MeshCpu* mesh_for_obj_export(Document& document) const;
  int find_open_document(const QString& path) const;
  DocumentViewport* current_viewport() const;
  DrawingView* current_drawing_view() const;
  static bool is_obj_path(const QString& path);
  static bool is_tdoc_path(const QString& path);
  static bool is_trscn_path(const QString& path);

  QStackedWidget* stack_ = nullptr;
  HomePage* home_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  RibbonBar* ribbon_ = nullptr;
  // 菜单栏本体归 RibbonBar（它和最上面那行工具图标是一块 widget），这里只留引用；
  // 两个动态子菜单要反复重建，也留一份。
  QMenuBar* menu_bar_ = nullptr;
  QMenu* recent_menu_ = nullptr;
  QMenu* window_menu_ = nullptr;
  // 顶层工具：新建 / 打开 / 保存 / 撤销 / 重做 / 编辑 / 工具 / 帮助。
  // 这些既在菜单里，也在最上面那行图标里（撤销 / 重做就是 FreeCAD 那种图标按钮）。
  QAction* new_action_ = nullptr;
  QAction* open_action_ = nullptr;
  QAction* open_drawing_action_ = nullptr;
  QAction* save_action_ = nullptr;
  QAction* save_as_action_ = nullptr;
  QAction* export_scene_action_ = nullptr;
  QAction* close_tab_action_ = nullptr;
  QAction* next_tab_action_ = nullptr;
  QAction* prev_tab_action_ = nullptr;
  QAction* exit_action_ = nullptr;
  QAction* undo_action_ = nullptr;
  QAction* redo_action_ = nullptr;
  QAction* move_action_ = nullptr;
  QAction* copy_action_ = nullptr;
  QAction* rotate_action_ = nullptr;
  QAction* mirror_action_ = nullptr;
  QAction* array_action_ = nullptr;
  QAction* frame_all_action_ = nullptr;
  QAction* home_action_ = nullptr;
  QAction* settings_action_ = nullptr;
  QAction* manage_action_ = nullptr;
  QAction* about_action_ = nullptr;
  QAction* pin_render_action_ = nullptr;
  QAction* debug_scene_action_ = nullptr;
  // 视口右侧工具列的入口（构件显隐 / 楼层 / 楼层视图 / 图纸管理）。
  QAction* components_action_ = nullptr;
  QAction* floors_action_ = nullptr;
  QAction* drawings_action_ = nullptr;
  // 停靠面板的显隐开关（属性 / 贴图库 / 句柄 / 计时 / 控制台）。
  QAction* property_toggle_ = nullptr;
  QAction* texture_toggle_ = nullptr;
  QAction* handle_toggle_ = nullptr;
  QAction* timing_toggle_ = nullptr;
  QAction* console_toggle_ = nullptr;
  RecentFilesStore recent_;
  QAction* wireframe_action_ = nullptr;
  QAction* shaded_action_ = nullptr;
  QAction* realistic_action_ = nullptr;
  QAction* xray_action_ = nullptr;
  QAction* grid_action_ = nullptr;
  // 图形诊断（帮助 → 图形诊断）：显示启动探测报告，一键复制给支持/IT。
  QAction* diagnostics_action_ = nullptr;
  // 注释：放一段文字注记（世界锚点 + 屏幕朝向，见 docs/TEXT.md §5）。
  QAction* text_action_ = nullptr;
  // 标注（文字）显示开关：轴号 / 标高 / 尺寸链，见 docs/TEXT.md §4.4。
  QAction* label_axis_action_ = nullptr;
  QAction* label_level_action_ = nullptr;
  QAction* label_dimension_action_ = nullptr;
  QAction* grid_settings_action_ = nullptr;
  QAction* drawing_visible_action_ = nullptr;
  QAction* trace_drawing_action_ = nullptr;
  QAction* wall_action_ = nullptr;
  QAction* beam_action_ = nullptr;
  QAction* column_action_ = nullptr;
  QAction* slab_action_ = nullptr;
  QAction* door_action_ = nullptr;
  QAction* window_action_ = nullptr;
  QAction* structural_wall_action_ = nullptr;
  QAction* foundation_action_ = nullptr;
  QAction* curtain_wall_action_ = nullptr;
  QAction* line_action_ = nullptr;
  QAction* polyline_action_ = nullptr;
  QAction* circle_action_ = nullptr;
  QAction* arc_action_ = nullptr;
  QAction* bezier_action_ = nullptr;
  QAction* bspline_action_ = nullptr;
  QAction* rectangle_action_ = nullptr;
  QAction* fillet_action_ = nullptr;
  QAction* chamfer_action_ = nullptr;
  QActionGroup* create_group_ = nullptr;
  PropertyPanel* property_panel_ = nullptr;
  HandleInspector* handle_inspector_ = nullptr;
  DrawPanel* draw_panel_ = nullptr;
  QDockWidget* draw_dock_ = nullptr;
  QAction* draw_toggle_ = nullptr;
  SceneDebuggerWindow* scene_debugger_ = nullptr;
  TextureLibraryPanel* texture_library_panel_ = nullptr;
  QDockWidget* texture_library_dock_ = nullptr;
  TimingPanel* timing_panel_ = nullptr;
  QDockWidget* timing_dock_ = nullptr;
  QAction* timing_record_action_ = nullptr;
  // 命令控制台：每次执行的内核命令的一行等价 C# 调用（默认收起）。
  ConsolePanel* console_panel_ = nullptr;
  QDockWidget* console_dock_ = nullptr;
  // AI 对话面板：进程内助手，工具直接调 SessionMcpBackend，不经 MCP 传输。
  AiPanel* ai_panel_ = nullptr;
  QDockWidget* ai_dock_ = nullptr;
  QAction* ai_toggle_ = nullptr;
  // 停靠布局的存盘节流（拖动面板时 LayoutRequest 连发，见 persist_window_state）。
  QTimer* window_state_timer_ = nullptr;
  bool window_state_ready_ = false;
  PluginHost plugin_host_;
  std::unique_ptr<McpService> mcp_;
  PluginManager plugin_manager_;
  struct PluginRibbonButton {
    std::string command_id;
    std::string plugin_id;
    std::string page_id;
    std::string group_id;
    RibbonGroup* group = nullptr;
    QToolButton* button = nullptr;
    QAction* action = nullptr;
  };
  std::vector<PluginRibbonButton> plugin_ribbon_buttons_;
  // 只是"目录里有动静"的探子；谁真的变了由托管侧的内容指纹决定。
  ExtensionWatcher* extension_watcher_ = nullptr;
  bool placed_on_primary_ = false;
};

}  // namespace tamias
