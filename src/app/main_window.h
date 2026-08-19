#pragma once

#include "engine/document/document.h"
#include "document_viewport.h"
#include "engine/render/render_runtime.h"
#include "home_page.h"
#include "engine/document/document_io.h"
#include "recent_files.h"

#include <QMainWindow>
#include <QStackedWidget>
#include <QTabWidget>
#include <memory>

class QAction;
class QActionGroup;

namespace tamias {

class PropertyPanel;
class HandleInspector;

class MainWindow final : public QMainWindow {
  Q_OBJECT
 public:
  explicit MainWindow(QWidget* parent = nullptr);

 private slots:
  void open_file();
  void save_file();
  void save_file_as();
  void frame_all();
  void close_tab(int index);
  void open_recent_path(const QString& path);
  void on_missing_recent(const QString& path);
  void open_settings();
  void show_home();
  void show_documents();
  void activate_open_document(int index);
  void refresh_property_panel();
  void refresh_handle_inspector();

 private:
  void add_document_tab(std::shared_ptr<Document> document,
                        const ViewportState* viewport = nullptr);
  Result<void> populate_document_meshes(Document& document, RenderThread& thread);
  void refresh_home();
  bool open_path(const QString& path);
  void new_document();
  void set_create_tool(ToolMode mode);
  void sync_create_tool_actions(ToolMode mode);
  bool write_selected_mesh(const QString& path);
  bool write_tdoc_document(const QString& path);
  void notify_save_success(const QString& path);
  void set_render_mode(RenderMode mode);
  void sync_render_mode_actions();
  const MeshCpu* selected_mesh(Document& document) const;
  const MeshCpu* mesh_for_obj_export(Document& document) const;
  int find_open_document(const QString& path) const;
  DocumentViewport* current_viewport() const;
  static bool is_obj_path(const QString& path);
  static bool is_tdoc_path(const QString& path);

  QStackedWidget* stack_ = nullptr;
  HomePage* home_ = nullptr;
  QTabWidget* tabs_ = nullptr;
  RecentFilesStore recent_;
  QAction* wireframe_action_ = nullptr;
  QAction* shaded_action_ = nullptr;
  QAction* realistic_action_ = nullptr;
  QAction* wall_action_ = nullptr;
  QAction* box_action_ = nullptr;
  QAction* cylinder_action_ = nullptr;
  QAction* beam_action_ = nullptr;
  QAction* column_action_ = nullptr;
  QAction* slab_action_ = nullptr;
  QAction* door_action_ = nullptr;
  QAction* window_action_ = nullptr;
  QAction* line_action_ = nullptr;
  QAction* polyline_action_ = nullptr;
  QAction* circle_action_ = nullptr;
  QAction* arc_action_ = nullptr;
  QAction* bezier_action_ = nullptr;
  QAction* rectangle_action_ = nullptr;
  QAction* fillet_action_ = nullptr;
  QAction* chamfer_action_ = nullptr;
  QActionGroup* create_group_ = nullptr;
  PropertyPanel* property_panel_ = nullptr;
  HandleInspector* handle_inspector_ = nullptr;
};

}  // namespace tamias
