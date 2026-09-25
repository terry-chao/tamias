#include "app/shell/main_window.h"

#include "app/shell/about_dialog.h"
#include "app/shell/console_panel.h"
#include "app/shell/extension_watcher.h"
#include "app/shell/graphics_diagnostics_dialog.h"
#include "app/base/app_settings.h"
#include "bim/ifc_spatial_tree.h"
#include "bim/wall_size.h"
#include "engine/base/executable_directory.h"
#include "engine/base/log.h"
#include "engine/document/document_io.h"
#include "engine/io/mesh_io.h"
#include "engine/render/scene/render_scene_golden.h"
#include "app/debug/golden_test_runner.h"
#include "app/shell/mesh_thumbnail.h"
#include "engine/modeling/occt/occt_shape_ops.h"
#include "engine/modeling/kernel/shape_ops.h"
#include "app/debug/handle_inspector.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"
#include "app/shell/plugin_manager_dialog.h"
#include "app/shell/plugin_prompt_dialog.h"
#include "app/debug/pin_result_dialog.h"
#include "app/bim/properties/property_panel.h"
#include "app/bim/components/draw_panel.h"
#include "app/drawing/drawing_document.h"
#include "app/drawing/drawing_import_dialog.h"
#include "app/drawing/drawing_view.h"
#include "app/bim/grid/grid_settings_dialog.h"
#include "app/base/qt_path.h"
#include "app/shell/ribbon_bar.h"
#include "app/shell/ribbon_group.h"
#include "app/shell/ribbon_page.h"
#include "app/edit/array_dialog.h"
#include "app/debug/scene_debugger_window.h"
#include "app/shell/settings_dialog.h"
#include "app/texture/texture_image.h"
#include "app/texture/texture_library_panel.h"
#include "app/debug/timing_panel.h"
#include "app/shell/toast.h"
#include "engine/profile/timing_scope.h"

#include <QByteArray>
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QEventLoop>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QShowEvent>
#include <QStyle>
#include <QStatusBar>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QProgressDialog>
#include <QSignalBlocker>
#include <QSize>
#include <QStatusBar>
#include <QToolButton>
#include <QTimer>
#include <QUrl>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tamias {
namespace {

// Ribbon 图标是彩色的轴测插画（FreeCAD 风格），按原色直接用，不再染成单色；
// 还走单色的只剩 QSS 里 checkbox 的勾选标记（check.svg / check_partial.svg）。
QIcon ribbon_icon(const QString& resource) { return QIcon(resource); }

// 选中构件在 XZ 上的中心（环形阵列中心的初值）。
Vec3 selection_centre_xz(DocumentViewport* viewport) {
  if (viewport == nullptr) {
    return {};
  }
  const Document& document = viewport->session().document();
  Aabb box{};
  bool any = false;
  for (const std::uint64_t id : document.selected_ids()) {
    const SceneNode* node = document.scene().find(id);
    if (node == nullptr || !node->world_bounds.valid()) {
      continue;
    }
    if (!any) {
      box = node->world_bounds;
      any = true;
    } else {
      box.expand(node->world_bounds.min);
      box.expand(node->world_bounds.max);
    }
  }
  if (!any) {
    return {};
  }
  return {(box.min.x + box.max.x) * 0.5f, 0.f, (box.min.z + box.max.z) * 0.5f};
}

void center_on_primary_screen(QWidget* widget) {
  QScreen* screen = QGuiApplication::primaryScreen();
  if (screen == nullptr || widget == nullptr) {
    return;
  }
  const QRect avail = screen->availableGeometry();
  const QSize size(qMin(widget->width(), avail.width()), qMin(widget->height(), avail.height()));
  if (size != widget->size()) {
    widget->resize(size);
  }
  widget->setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size, avail));
}

void reveal_path(const std::filesystem::path& path) {
  QDesktopServices::openUrl(QUrl::fromLocalFile(path_to_qstring(path)));
}

class OpenProgressDialog final {
 public:
  OpenProgressDialog(QWidget* parent, const QString& title, const QString& label)
      : dialog_(title, label, 0, 100, parent) {
    dialog_.setWindowTitle(title);
    dialog_.setWindowModality(Qt::WindowModal);
    dialog_.setMinimumDuration(0);
    dialog_.setAutoClose(false);
    dialog_.setAutoReset(false);
    dialog_.setCancelButton(nullptr);
    dialog_.setValue(0);
    dialog_.show();
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }

  ~OpenProgressDialog() { close(); }

  OpenProgressDialog(const OpenProgressDialog&) = delete;
  OpenProgressDialog& operator=(const OpenProgressDialog&) = delete;

  void stage(int percent, const QString& label) {
    if (dialog_.maximum() == 0) {
      dialog_.setRange(0, 100);
    }
    dialog_.setLabelText(label);
    dialog_.setValue(std::clamp(percent, 0, 100));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }

  void busy(const QString& label) {
    dialog_.setLabelText(label);
    dialog_.setRange(0, 0);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
  }

  void close() {
    if (dialog_.isVisible()) {
      dialog_.close();
    }
  }

 private:
  QProgressDialog dialog_;
};

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), plugin_manager_(plugin_host_) {
  setWindowTitle("Tamias");
  setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));
  resize(1800, 1000);
  center_on_primary_screen(this);

  recent_.load();
  AppSettings::instance().load();

  stack_ = new QStackedWidget(this);
  home_ = new HomePage(stack_);
  tabs_ = new QTabWidget(stack_);
  tabs_->setTabsClosable(true);
  tabs_->setDocumentMode(true);
  stack_->addWidget(home_);
  stack_->addWidget(tabs_);
  setCentralWidget(stack_);

  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab);
  connect(tabs_, &QTabWidget::currentChanged, this, [this](int) {
    for (int i = 0; i < tabs_->count(); ++i) {
      if (i != tabs_->currentIndex()) {
        if (auto* viewport = qobject_cast<DocumentViewport*>(tabs_->widget(i))) {
          viewport->cancel_plugin_point_input();
        }
      }
    }
    sync_render_mode_actions();
    sync_bim_actions();
    refresh_property_panel();
    refresh_handle_inspector();
    refresh_texture_library_panel();
    sync_draw_panel();
    bind_plugin_session();
    sync_document_actions();
  });
  connect(home_, &HomePage::openRequested, this, &MainWindow::open_file);
  connect(home_, &HomePage::newRequested, this, &MainWindow::new_document);
  connect(home_, &HomePage::fileActivated, this, &MainWindow::open_recent_path);
  connect(home_, &HomePage::missingFileActivated, this, &MainWindow::on_missing_recent);
  connect(home_, &HomePage::recentRemoveRequested, this, [this](const QString& path) {
    recent_.remove(path);
    refresh_home();
  });
  connect(home_, &HomePage::openDocumentActivated, this, &MainWindow::activate_open_document);
  connect(home_, &HomePage::settingsRequested, this, &MainWindow::open_settings);

  new_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/new.svg")),
                            tr("New"), this);
  new_action_->setShortcut(QKeySequence::New);
  new_action_->setToolTip(tr("New document"));
  connect(new_action_, &QAction::triggered, this, &MainWindow::new_document);
  addAction(new_action_);

  open_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/open.svg")),
                             tr("Open"), this);
  open_action_->setShortcut(QKeySequence::Open);
  open_action_->setToolTip(tr("Open a model file"));
  connect(open_action_, &QAction::triggered, this, &MainWindow::open_file);
  addAction(open_action_);

  open_drawing_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/drawing.svg")),
                  tr("Open Drawing"), this);
  open_drawing_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
  open_drawing_action_->setToolTip(
      tr("Attach a reference drawing (PDF / DXF / SVG / image) to the open document — it is "
         "drawn under the model in the viewport"));
  connect(open_drawing_action_, &QAction::triggered, this, &MainWindow::open_drawing_file);
  addAction(open_drawing_action_);

  save_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/save.svg")),
                             tr("Save"), this);
  save_action_->setShortcut(QKeySequence::Save);
  save_action_->setToolTip(tr("Save the document"));
  connect(save_action_, &QAction::triggered, this, &MainWindow::save_file);
  addAction(save_action_);

  save_as_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/save_as.svg")),
                                tr("Save As"), this);
  save_as_action_->setShortcut(QKeySequence::SaveAs);
  save_as_action_->setToolTip(tr("Save the document to a new file"));
  connect(save_as_action_, &QAction::triggered, this, &MainWindow::save_file_as);
  addAction(save_as_action_);

  pin_render_action_ = new QAction(tr("Pin Render Scene for Tests"), this);
  pin_render_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  pin_render_action_->setToolTip(
      tr("Write the current view to assets/samples/render/<name>/ and run RenderSceneGolden*"));
  connect(pin_render_action_, &QAction::triggered, this, &MainWindow::pin_render_scene_golden);
  addAction(pin_render_action_);

  debug_scene_action_ = new QAction(tr("Debug This Frame"), this);
  debug_scene_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
  debug_scene_action_->setToolTip(
      tr("Capture the current viewport's draw list and open the scene debugger"));
  connect(debug_scene_action_, &QAction::triggered, this, &MainWindow::debug_current_frame);
  addAction(debug_scene_action_);
  auto* debug_scene_alias = new QAction(this);
  debug_scene_alias->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+I")));
  connect(debug_scene_alias, &QAction::triggered, this, &MainWindow::debug_current_frame);
  addAction(debug_scene_alias);

  frame_all_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/frame_all.svg")),
                                  tr("Fit All"), this);
  frame_all_action_->setShortcut(QKeySequence(tr("F")));
  frame_all_action_->setToolTip(tr("Frame all geometry in the view"));
  connect(frame_all_action_, &QAction::triggered, this, &MainWindow::frame_all);
  addAction(frame_all_action_);

  create_group_ = new QActionGroup(this);
  create_group_->setExclusive(true);

  wall_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/wall.svg")),
                             tr("Wall"), this);
  wall_action_->setCheckable(true);
  wall_action_->setProperty("toolMode", static_cast<int>(ToolMode::Wall));
  wall_action_->setToolTip(tr("Create a wall: click start, then click end"));
  connect(wall_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Wall); });
  create_group_->addAction(wall_action_);
  addAction(wall_action_);

  beam_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/beam.svg")),
                             tr("Beam"), this);
  beam_action_->setCheckable(true);
  beam_action_->setProperty("toolMode", static_cast<int>(ToolMode::Beam));
  beam_action_->setToolTip(tr("Create a beam: click start, then click end"));
  connect(beam_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Beam); });
  create_group_->addAction(beam_action_);
  addAction(beam_action_);

  column_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/column.svg")),
                               tr("Column"), this);
  column_action_->setCheckable(true);
  column_action_->setProperty("toolMode", static_cast<int>(ToolMode::Column));
  column_action_->setToolTip(tr("Create a column: click to place"));
  connect(column_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Column); });
  create_group_->addAction(column_action_);
  addAction(column_action_);

  slab_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/slab.svg")),
                             tr("Slab"), this);
  slab_action_->setCheckable(true);
  slab_action_->setProperty("toolMode", static_cast<int>(ToolMode::Slab));
  slab_action_->setToolTip(
      tr("Create a slab in plan view: click two opposite corners"));
  connect(slab_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Slab); });
  create_group_->addAction(slab_action_);
  addAction(slab_action_);

  door_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/door.svg")),
                             tr("Door"), this);
  door_action_->setCheckable(true);
  door_action_->setProperty("toolMode", static_cast<int>(ToolMode::Door));
  door_action_->setToolTip(tr("Create a door: click a wall to host it"));
  connect(door_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Door); });
  create_group_->addAction(door_action_);
  addAction(door_action_);

  window_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/window.svg")),
                               tr("Window"), this);
  window_action_->setCheckable(true);
  window_action_->setProperty("toolMode", static_cast<int>(ToolMode::Window));
  window_action_->setToolTip(tr("Create a window: click a wall to host it"));
  connect(window_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Window); });
  create_group_->addAction(window_action_);
  addAction(window_action_);

  structural_wall_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/structural_wall.svg")),
                                        tr("Struct. Wall"), this);
  structural_wall_action_->setCheckable(true);
  structural_wall_action_->setProperty("toolMode", static_cast<int>(ToolMode::StructuralWall));
  structural_wall_action_->setToolTip(tr("Create a structural / shear wall: click start, then end"));
  connect(structural_wall_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::StructuralWall); });
  create_group_->addAction(structural_wall_action_);
  addAction(structural_wall_action_);

  foundation_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/foundation.svg")),
                                   tr("Foundation"), this);
  foundation_action_->setCheckable(true);
  foundation_action_->setProperty("toolMode", static_cast<int>(ToolMode::Foundation));
  foundation_action_->setToolTip(tr("Create a foundation: click to place"));
  connect(foundation_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Foundation); });
  create_group_->addAction(foundation_action_);
  addAction(foundation_action_);

  curtain_wall_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/curtain_wall.svg")),
                                     tr("Curtain Wall"), this);
  curtain_wall_action_->setCheckable(true);
  curtain_wall_action_->setProperty("toolMode", static_cast<int>(ToolMode::CurtainWall));
  curtain_wall_action_->setToolTip(tr("Create a curtain wall: click start, then end"));
  connect(curtain_wall_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::CurtainWall); });
  create_group_->addAction(curtain_wall_action_);
  addAction(curtain_wall_action_);

  line_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/line.svg")),
                             tr("Line"), this);
  line_action_->setCheckable(true);
  line_action_->setProperty("toolMode", static_cast<int>(ToolMode::Line));
  line_action_->setToolTip(tr("Create a line: click start, then click end"));
  connect(line_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Line); });
  create_group_->addAction(line_action_);
  addAction(line_action_);

  polyline_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/polyline.svg")),
                                 tr("Polyline"), this);
  polyline_action_->setCheckable(true);
  polyline_action_->setProperty("toolMode", static_cast<int>(ToolMode::Polyline));
  polyline_action_->setToolTip(tr("Create a polyline: click points, Enter or double-click to finish"));
  connect(polyline_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Polyline); });
  create_group_->addAction(polyline_action_);
  addAction(polyline_action_);

  rectangle_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/rectangle.svg")),
                                  tr("Rectangle"), this);
  rectangle_action_->setCheckable(true);
  rectangle_action_->setProperty("toolMode", static_cast<int>(ToolMode::Rectangle));
  rectangle_action_->setToolTip(tr("Create a rectangle: click two opposite corners"));
  connect(rectangle_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Rectangle); });
  create_group_->addAction(rectangle_action_);
  addAction(rectangle_action_);

  circle_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/circle.svg")),
                               tr("Circle"), this);
  circle_action_->setCheckable(true);
  circle_action_->setProperty("toolMode", static_cast<int>(ToolMode::Circle));
  circle_action_->setToolTip(tr("Create a circle: click center, then click radius"));
  connect(circle_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Circle); });
  create_group_->addAction(circle_action_);
  addAction(circle_action_);

  arc_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/arc.svg")),
                            tr("Arc"), this);
  arc_action_->setCheckable(true);
  arc_action_->setProperty("toolMode", static_cast<int>(ToolMode::Arc));
  arc_action_->setToolTip(tr("Create an arc: click start, through, then end"));
  connect(arc_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Arc); });
  create_group_->addAction(arc_action_);
  addAction(arc_action_);

  bezier_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/bezier.svg")),
                               tr("Bezier"), this);
  bezier_action_->setCheckable(true);
  bezier_action_->setProperty("toolMode", static_cast<int>(ToolMode::Bezier));
  bezier_action_->setToolTip(
      tr("Create a Bezier: click control points, Enter or double-click to finish"));
  connect(bezier_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Bezier); });
  create_group_->addAction(bezier_action_);
  addAction(bezier_action_);

  bspline_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/bspline.svg")),
                                tr("B-spline"), this);
  bspline_action_->setCheckable(true);
  bspline_action_->setProperty("toolMode", static_cast<int>(ToolMode::BSpline));
  bspline_action_->setToolTip(
      tr("Create a B-spline: click control points, Enter or double-click to finish"));
  connect(bspline_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::BSpline); });
  create_group_->addAction(bspline_action_);
  addAction(bspline_action_);

  fillet_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/fillet.svg")),
                               tr("Fillet"), this);
  fillet_action_->setToolTip(tr("Fillet the selected entity's first edge"));
  connect(fillet_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->fillet_selected(0.05);
    }
  });
  addAction(fillet_action_);

  chamfer_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/chamfer.svg")),
                                tr("Chamfer"), this);
  chamfer_action_->setToolTip(tr("Chamfer the selected entity's first edge"));
  connect(chamfer_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->chamfer_selected(0.05);
    }
  });
  addAction(chamfer_action_);

  // 通用编辑：移动 / 复制 / 旋转 / 镜像 / 阵列（见 command/edit/entity_transform.h）。
  // 前四个是「点基点 → 点目标点」的交互式工具；阵列收参数后一次落位。
  move_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/move.svg")),
                             tr("Move"), this);
  move_action_->setShortcut(QKeySequence(tr("Ctrl+M")));
  move_action_->setToolTip(tr("Move the selection: click a base point, then the target point"));
  connect(move_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->begin_move_selection();
    }
  });
  addAction(move_action_);

  copy_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/copy.svg")),
                             tr("Copy"), this);
  copy_action_->setShortcut(QKeySequence(tr("Ctrl+K")));
  copy_action_->setToolTip(tr("Copy the selection: click a base point, then the target point "
                             "(walls bring their doors and windows along)"));
  connect(copy_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->begin_copy_selection();
    }
  });
  addAction(copy_action_);

  rotate_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/rotate.svg")),
                               tr("Rotate"), this);
  rotate_action_->setShortcut(QKeySequence(tr("Ctrl+R")));
  rotate_action_->setToolTip(tr("Rotate the selection about a vertical axis: base point, "
                               "reference direction, target direction"));
  connect(rotate_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->begin_rotate_selection();
    }
  });
  addAction(rotate_action_);

  mirror_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/mirror.svg")),
                               tr("Mirror"), this);
  mirror_action_->setShortcut(QKeySequence(tr("Ctrl+Shift+M")));
  mirror_action_->setToolTip(tr("Mirror the selection: click the two ends of the mirror axis"));
  connect(mirror_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->begin_mirror_selection();
    }
  });
  addAction(mirror_action_);

  array_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/array.svg")),
                              tr("Array"), this);
  array_action_->setShortcut(QKeySequence(tr("Ctrl+Shift+A")));
  array_action_->setToolTip(tr("Array the selection: repeat it linearly or around a centre"));
  connect(array_action_, &QAction::triggered, this, [this] {
    DocumentViewport* vp = current_viewport();
    if (vp == nullptr) {
      return;
    }
    const Vec3 centre = selection_centre_xz(vp);
    ArrayDialog dialog(this, centre.x, centre.z);
    if (dialog.exec() != QDialog::Accepted) {
      return;
    }
    const ArrayDialog::Params params = dialog.params();
    DocumentViewport::ArrayParams out;
    out.polar = params.polar;
    out.count = params.count;
    out.spacing = params.spacing;
    out.step_angle = params.step_angle;
    out.center = {static_cast<float>(params.centre_x), 0.f, static_cast<float>(params.centre_z)};
    vp->array_selection(out);
  });
  addAction(array_action_);

  settings_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/settings.svg")),
                                 tr("Settings"), this);
  settings_action_->setShortcut(QKeySequence(tr("Ctrl+,")));
  settings_action_->setToolTip(tr("Open settings"));
  connect(settings_action_, &QAction::triggered, this, &MainWindow::open_settings);
  addAction(settings_action_);

  about_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/about.svg")),
                              tr("About"), this);
  about_action_->setToolTip(tr("About Tamias"));
  connect(about_action_, &QAction::triggered, this, &MainWindow::open_about);
  addAction(about_action_);

  // 图形诊断：把启动探测报告摊开给用户 / IT 看（一键复制）。比像素金样更贴近交付现场。
  diagnostics_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/gpu.svg")),
                                    tr("Graphics Diagnostics"), this);
  diagnostics_action_->setToolTip(
      tr("Show which graphics backend this machine uses and why (copy for support)"));
  connect(diagnostics_action_, &QAction::triggered, this, &MainWindow::open_graphics_diagnostics);
  addAction(diagnostics_action_);

  manage_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/settings.svg")),
                               tr("Plugin Manager"), this);
  manage_action_->setToolTip(tr("Choose which loaded plugins appear on the ribbon"));
  connect(manage_action_, &QAction::triggered, this, &MainWindow::open_plugin_manager);
  addAction(manage_action_);

  home_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/home.svg")),
                             tr("Welcome"), this);
  home_action_->setToolTip(tr("Back to the welcome page"));
  connect(home_action_, &QAction::triggered, this, &MainWindow::show_home);
  addAction(home_action_);

  undo_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/undo.svg")),
                             tr("Undo"), this);
  undo_action_->setShortcut(QKeySequence::Undo);
  connect(undo_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->undo();
    }
  });
  addAction(undo_action_);

  redo_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/redo.svg")),
                             tr("Redo"), this);
  redo_action_->setShortcut(QKeySequence::Redo);
  connect(redo_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->redo();
    }
  });
  addAction(redo_action_);

  exit_action_ = new QAction(tr("E&xit"), this);
  exit_action_->setShortcut(QKeySequence::Quit);
  connect(exit_action_, &QAction::triggered, this, &QWidget::close);
  addAction(exit_action_);

  auto* display_group = new QActionGroup(this);
  display_group->setExclusive(true);

  wireframe_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/wireframe.svg")),
                                  tr("Wireframe"), this);
  wireframe_action_->setCheckable(true);
  wireframe_action_->setShortcut(QKeySequence(tr("Ctrl+1")));
  wireframe_action_->setToolTip(tr("Line drawing — edges only"));
  display_group->addAction(wireframe_action_);
  connect(wireframe_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Wireframe);
  });
  addAction(wireframe_action_);

  shaded_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/shaded.svg")),
                               tr("Shaded"), this);
  shaded_action_->setCheckable(true);
  shaded_action_->setChecked(true);
  shaded_action_->setShortcut(QKeySequence(tr("Ctrl+2")));
  shaded_action_->setToolTip(tr("Simple shaded solid display"));
  display_group->addAction(shaded_action_);
  connect(shaded_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Shaded);
  });
  addAction(shaded_action_);

  realistic_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/realistic.svg")),
                                  tr("Realistic"), this);
  realistic_action_->setCheckable(true);
  realistic_action_->setShortcut(QKeySequence(tr("Ctrl+3")));
  realistic_action_->setToolTip(tr("Lit display with specular highlights"));
  display_group->addAction(realistic_action_);
  connect(realistic_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Realistic);
  });
  addAction(realistic_action_);

  // X 光（X-Ray）：不是第四种显示模式，而是叠在显示模式上的开关——工业软件里
  // 也放在显示模式旁边而不是塞进那个列表，因为「透视 + 着色」「透视 + 真实感」
  // 都要能用。全场景半透明，用来一眼看穿整栋楼。
  xray_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/xray.svg")), tr("X-Ray"), this);
  xray_action_->setCheckable(true);
  xray_action_->setShortcut(QKeySequence(tr("Ctrl+4")));
  xray_action_->setToolTip(tr("See through everything — all components semi-transparent"));
  connect(xray_action_, &QAction::toggled, this, [this](bool on) {
    if (auto* vp = current_viewport()) {
      vp->set_xray(on);
    }
  });
  addAction(xray_action_);

  // 轴网：显示开关 + 设置。轴网是定位参考（不是构件），只在视口画线、不进实体表。
  grid_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/grid.svg")), tr("Grid"), this);
  grid_action_->setCheckable(true);
  grid_action_->setChecked(true);
  grid_action_->setToolTip(tr("Show the structural grid"));
  connect(grid_action_, &QAction::triggered, this, [this](bool on) {
    if (auto* vp = current_viewport()) {
      vp->set_grid_visible(on);
    }
  });
  addAction(grid_action_);

  grid_settings_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/settings.svg")), tr("Grid Settings"), this);
  grid_settings_action_->setToolTip(tr("Create or edit the structural grid"));
  connect(grid_settings_action_, &QAction::triggered, this, [this] {
    auto* vp = current_viewport();
    if (vp == nullptr) {
      return;
    }
    GridSettingsDialog dialog(vp->document().bim().grid().axes(), this);
    if (dialog.exec() != QDialog::Accepted) {
      return;
    }
    if (!dialog.place_with_click()) {
      vp->apply_grid_settings(dialog.axes());
      return;
    }
    // 布置完还要落位：把轴网显示打开（否则放完看不见），再让视口接管点击放置。
    vp->set_grid_visible(true);
    if (grid_action_ != nullptr) {
      const QSignalBlocker block(grid_action_);
      grid_action_->setChecked(true);
    }
    vp->begin_grid_placement(dialog.axes(), dialog.placement_anchor());
  });
  addAction(grid_settings_action_);

  // 注释 → 文字：点一下落位，弹框输入。一步撤销（create_text 命令）。
  text_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/text_label.svg")), tr("Text"),
                             this);
  text_action_->setShortcut(QKeySequence(tr("Ctrl+Shift+T")));
  text_action_->setToolTip(tr("Place a text annotation: click a point, then type"));
  connect(text_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->begin_text_placement();
    }
  });
  addAction(text_action_);

  // 文字标注：轴号 / 标高 / 尺寸链。都是派生标注（不落盘），只控制这一帧画不画。
  const auto make_label_action = [this](const char* icon, const QString& text,
                                        const QString& tip, TextKind kind) {
    auto* action = new QAction(ribbon_icon(QString::fromLatin1(icon)), text, this);
    action->setCheckable(true);
    action->setChecked(true);
    action->setToolTip(tip);
    connect(action, &QAction::triggered, this, [this, kind](bool on) {
      if (auto* vp = current_viewport()) {
        vp->set_label_kind_visible(kind, on);
      }
    });
    addAction(action);
    return action;
  };
  label_axis_action_ = make_label_action(":/icons/text_label.svg", tr("Axis Tags"),
                                         tr("Show grid axis tags (A, B, 1, 2 …)"),
                                         TextKind::AxisLabel);
  label_level_action_ = make_label_action(":/icons/level.svg", tr("Levels"),
                                          tr("Show storey names and elevations"),
                                          TextKind::StoreyLabel);
  label_dimension_action_ =
      make_label_action(":/icons/dimension.svg", tr("Dimensions"),
                        tr("Show grid spacing dimensions (plan view)"), TextKind::Dimension);

  // 参考图纸底图总开关：图纸挂在文档下（「图纸管理」面板），画在视口里模型之下。
  drawing_visible_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/drawing.svg")),
                                        tr("Reference Drawings"), this);
  drawing_visible_action_->setCheckable(true);
  drawing_visible_action_->setChecked(true);
  drawing_visible_action_->setToolTip(
      tr("Show or hide the reference drawings attached to this document; the Drawings panel "
         "manages them"));
  connect(drawing_visible_action_, &QAction::triggered, this, [this](bool on) {
    auto* vp = current_viewport();
    if (vp == nullptr) {
      return;
    }
    vp->set_drawings_visible(on);
  });
  addAction(drawing_visible_action_);

  // 翻模：从 DXF 平面图生成墙 / 柱 / 门窗。识别结果先给用户复核，再落地。
  trace_drawing_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/tracing.svg")), tr("Trace Drawing"), this);
  trace_drawing_action_->setToolTip(
      tr("Create walls, columns and doors/windows from a DXF floor plan"));
  connect(trace_drawing_action_, &QAction::triggered, this, [this] {
    auto* vp = current_viewport();
    if (vp == nullptr) {
      statusBar()->showMessage(tr("Open or create a model first."), 6000);
      return;
    }
    // 先看停在的二维图纸页（只有 DXF 能翻模），否则用文档里挂着的第一张 DXF。
    QString path;
    if (DrawingView* drawing = current_drawing_view()) {
      const QString candidate = drawing->document().path();
      if (QFileInfo(candidate).suffix().compare(QStringLiteral("dxf"), Qt::CaseInsensitive) == 0) {
        path = candidate;
      }
    }
    if (path.isEmpty()) {
      for (const DrawingRef& ref : vp->document().drawings()) {
        const QString candidate = QString::fromStdString(ref.path);
        if (QFileInfo(candidate).suffix().compare(QStringLiteral("dxf"), Qt::CaseInsensitive) == 0) {
          path = candidate;
          break;
        }
      }
    }
    DrawingImportDialog dialog(path, &vp->document().bim().grid(), this);
    if (dialog.exec() != QDialog::Accepted || dialog.plan().empty()) {
      return;
    }
    vp->apply_drawing_import(dialog.plan());
  });
  addAction(trace_drawing_action_);

  // 右侧属性面板：展示/编辑选中实体的参数。
  property_panel_ = new PropertyPanel(this);
  auto* property_dock = new QDockWidget(tr("Properties"), this);
  property_dock->setObjectName(QStringLiteral("propertyDock"));
  property_dock->setWidget(property_panel_);
  property_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, property_dock);
  property_toggle_ = property_dock->toggleViewAction();
  property_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/properties.svg")));
  addAction(property_toggle_);

  // 左侧绘制设置面板：点构件 icon 后在此选子类型/参数，再"开始绘制"武装命令。
  // 默认收起——没有文档时它没有意义；用构件工具或"视图 · 面板 · 绘制设置"唤出。
  draw_panel_ = new DrawPanel(this);
  // 板的"标高偏移"默认取当前楼层层高（= 本层顶板）。层高在文档里，面板拿不到，
  // 这里注入；没有模型文档就退回默认层高。
  draw_panel_->set_storey_height_provider([this] {
    DocumentViewport* vp = current_viewport();
    const Storey* active =
        vp != nullptr ? vp->document().bim().find_storey(
                            vp->document().bim().active_storey_id())
                      : nullptr;
    return active != nullptr && active->height > 0.0 ? active->height : kDefaultWallHeight;
  });
  draw_dock_ = new QDockWidget(tr("Draw"), this);
  draw_dock_->setObjectName(QStringLiteral("drawDock"));
  draw_dock_->setWidget(draw_panel_);
  draw_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::LeftDockWidgetArea, draw_dock_);
  draw_dock_->hide();
  draw_toggle_ = draw_dock_->toggleViewAction();
  draw_toggle_->setText(tr("Draw Settings"));
  draw_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/draw_panel.svg")));
  draw_toggle_->setToolTip(tr("Show the draw settings panel"));
  addAction(draw_toggle_);
  connect(draw_panel_, &DrawPanel::armed_args, this, [this](ToolMode mode, const CommandArgs& args) {
    if (auto* vp = current_viewport()) {
      vp->arm_create(mode, args);
    }
  });
  connect(draw_panel_, &DrawPanel::disarmed, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->set_tool(ToolMode::None);
    }
  });

  texture_library_panel_ = new TextureLibraryPanel(this);
  texture_library_dock_ = new QDockWidget(tr("Texture Library"), this);
  texture_library_dock_->setObjectName(QStringLiteral("textureLibraryDock"));
  texture_library_dock_->setWidget(texture_library_panel_);
  texture_library_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, texture_library_dock_);
  tabifyDockWidget(property_dock, texture_library_dock_);
  property_dock->raise();
  connect(texture_library_dock_, &QDockWidget::visibilityChanged, this, [this](bool visible) {
    if (visible) {
      refresh_texture_library_panel();
    }
  });
  texture_toggle_ = texture_library_dock_->toggleViewAction();
  texture_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/texture_library.svg")));
  addAction(texture_toggle_);


  handle_inspector_ = new HandleInspector(this);
  auto* handle_dock = new QDockWidget(tr("Handle Inspector"), this);
  handle_dock->setObjectName(QStringLiteral("handleInspectorDock"));
  handle_dock->setWidget(handle_inspector_);
  handle_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, handle_dock);
  handle_toggle_ = handle_dock->toggleViewAction();
  handle_toggle_->setText(tr("Inspector"));
  handle_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/inspector.svg")));
  handle_toggle_->setShortcut(QKeySequence(tr("Ctrl+D")));
  handle_toggle_->setToolTip(tr("Inspect the selected component's document handle"));
  addAction(handle_toggle_);
  connect(handle_inspector_, &HandleInspector::locate_requested, this,
          [this](std::uint64_t node_id) {
            if (auto* vp = current_viewport()) {
              vp->frame_node(node_id);
            }
          });

  timing_panel_ = new TimingPanel(this);
  timing_panel_->setMinimumWidth(360);
  timing_dock_ = new QDockWidget(tr("Timing"), this);
  timing_dock_->setObjectName(QStringLiteral("timingDock"));
  timing_dock_->setWidget(timing_panel_);
  timing_dock_->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea |
                                Qt::BottomDockWidgetArea);
  timing_dock_->setMinimumWidth(360);
  addDockWidget(Qt::BottomDockWidgetArea, timing_dock_);
  timing_dock_->hide();
  timing_toggle_ = timing_dock_->toggleViewAction();
  timing_toggle_->setText(tr("Timing"));
  timing_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/timing.svg")));
  timing_toggle_->setToolTip(tr("Show the timing timeline"));
  addAction(timing_toggle_);

  timing_record_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/timing.svg")),
                                      tr("Record"), this);
  timing_record_action_->setCheckable(true);
  timing_record_action_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
  timing_record_action_->setToolTip(tr("Start recording, then stop to inspect the timeline"));
  addAction(timing_record_action_);

  // 命令控制台：底部停靠，默认收起。每次真正执行的内核命令长一行等价 C# 调用
  // （见 host/command_echo.h）——点着学 API，抄走改参数就能重跑。
  console_panel_ = new ConsolePanel(this);
  console_dock_ = new QDockWidget(tr("Command Console"), this);
  console_dock_->setObjectName(QStringLiteral("consoleDock"));
  console_dock_->setWidget(console_panel_);
  console_dock_->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::LeftDockWidgetArea |
                                 Qt::RightDockWidgetArea);
  addDockWidget(Qt::BottomDockWidgetArea, console_dock_);
  console_dock_->hide();
  console_toggle_ = console_dock_->toggleViewAction();
  console_toggle_->setText(tr("Command Console"));
  console_toggle_->setIcon(ribbon_icon(QStringLiteral(":/icons/console.svg")));
  console_toggle_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+J")));
  console_toggle_->setToolTip(
      tr("Show the command echo: every executed kernel command as a paste-ready C# call"));
  addAction(console_toggle_);

  connect(console_panel_, &ConsolePanel::run_requested, this, [this](const QString& code) {
    // 回显输入：多行只回第一行，免得刷屏。
    QString echo = code.section(QLatin1Char('\n'), 0, 0);
    if (echo.size() < code.size()) {
      echo += QStringLiteral(" …");
    }
    console_panel_->append_line(QStringLiteral("> ") + echo, ConsolePanel::LineStyle::Input);
    // 求值在 Tamias.Host 的脚本引擎里做：`host` 就是当前文档的宿主，整段自带一个事务。
    const auto result = plugin_host_.evaluate(code.toStdString());
    if (result) {
      if (!result->empty()) {
        console_panel_->append_line(QString::fromStdString(*result));
      }
    } else {
      const QString error = QString::fromStdString(result.error());
      console_panel_->append_line(error, ConsolePanel::LineStyle::Error);
      console_panel_->focus_error_line(error);  // 报错带 (行,列) 就把光标带过去
    }
  });

  connect(timing_record_action_, &QAction::toggled, this, [this](bool checked) {
    if (checked) {
      timing_dock_->show();
      timing_dock_->raise();
    }
    timing_panel_->set_recording(checked);
  });
  connect(timing_panel_, &TimingPanel::recording_changed, this, [this](bool recording) {
    QSignalBlocker block(timing_record_action_);
    timing_record_action_->setChecked(recording);
    timing_record_action_->setText(recording ? tr("Stop") : tr("Record"));
    if (recording) {
      timing_dock_->show();
      timing_dock_->raise();
    }
  });

  debug_scene_action_->setIcon(ribbon_icon(QStringLiteral(":/icons/shaded.svg")));
  debug_scene_action_->setText(tr("Scene Debugger"));

  auto* ribbon = new RibbonBar(this);
  ribbon_ = ribbon;
  // 两种形态：图标 + 文字（现状）/ 仅图标（FreeCAD 那种，悬浮出提示）。
  // 右上角的小按钮和「设置 → 界面」都能切，选哪个记在设置里。
  ribbon->set_style_button_icon(ribbon_icon(QStringLiteral(":/icons/ribbon_style.svg")));
  ribbon->set_display_mode(AppSettings::instance().ribbon_style() == QStringLiteral("icons")
                               ? RibbonDisplayMode::IconOnly
                               : RibbonDisplayMode::IconWithText);
  connect(ribbon, &RibbonBar::display_mode_changed, this, [](RibbonDisplayMode mode) {
    auto& settings = AppSettings::instance();
    settings.set_ribbon_style(mode == RibbonDisplayMode::IconOnly ? QStringLiteral("icons")
                                                                 : QStringLiteral("text"));
    settings.save();
  });
  connect(ribbon, &RibbonBar::floating_groups_changed, this, [this] {
    if (ribbon_ == nullptr) {
      return;
    }
    auto& settings = AppSettings::instance();
    settings.set_ribbon_floating_groups(ribbon_->floating_group_keys());
    settings.save();
  });
  // 拖动分组（换排 / 换位置）之后立刻记下来：「这次拖完是什么样，下次开还是什么样」。
  connect(ribbon, &RibbonBar::layout_changed, this, [this] {
    if (ribbon_ == nullptr) {
      return;
    }
    auto& settings = AppSettings::instance();
    settings.set_ribbon_layout(ribbon_->layout_keys());
    settings.save();
  });
  // 卷起 / 展开也属于布局，一起记。
  connect(ribbon, &RibbonBar::collapsed_changed, this, [](bool collapsed) {
    auto& settings = AppSettings::instance();
    settings.set_ribbon_collapsed(collapsed);
    settings.save();
  });
  // 菜单下面只有这一条工具带：菜单栏那行不再挂「新建 / 打开 / 保存 / 撤销 / 重做」
  // 那一排小图标——新建 / 打开 / 保存 在「文件」组里本来就有，撤销 / 重做 归下边的
  // 「编辑」组（见 build_menu_bar 里那条 编辑 菜单，命令是同一批 QAction）。
  RibbonPage* home_page = ribbon->add_page(QStringLiteral("home"), tr("Home"));
  RibbonGroup* file_group = home_page->add_group(QStringLiteral("file"), tr("File"));
  file_group->add_action(new_action_);
  file_group->add_action(open_action_);
  file_group->add_action(open_drawing_action_);
  file_group->add_action(trace_drawing_action_);
  file_group->add_action(save_action_);
  file_group->add_action(save_as_action_);

  // 撤销 / 重做：住工具带而不是菜单栏那排小图标。放在「文件」组右边，
  // 和菜单栏的「文件 → 编辑」是一个顺序。
  RibbonGroup* edit_group = home_page->add_group(QStringLiteral("edit"), tr("Edit"));
  edit_group->add_action(undo_action_);
  edit_group->add_action(redo_action_);

  RibbonGroup* draw_group = home_page->add_group(QStringLiteral("draw"), tr("Draw"));
  draw_group->add_action(line_action_);
  draw_group->add_action(polyline_action_);
  draw_group->add_action(rectangle_action_);
  draw_group->add_action(circle_action_);
  draw_group->add_action(arc_action_);
  draw_group->add_action(bezier_action_);
  draw_group->add_action(bspline_action_);

  RibbonGroup* architectural_group =
      home_page->add_group(QStringLiteral("architectural"), tr("Architectural"));
  architectural_group->add_action(wall_action_);
  architectural_group->add_action(door_action_);
  architectural_group->add_action(window_action_);
  architectural_group->add_action(curtain_wall_action_);

  RibbonGroup* structural_group =
      home_page->add_group(QStringLiteral("structural"), tr("Structural"));
  structural_group->add_action(beam_action_);
  structural_group->add_action(column_action_);
  structural_group->add_action(slab_action_);
  structural_group->add_action(structural_wall_action_);
  structural_group->add_action(foundation_action_);

  RibbonGroup* modify_group = home_page->add_group(QStringLiteral("modify"), tr("Modify"));
  modify_group->add_action(move_action_);
  modify_group->add_action(copy_action_);
  modify_group->add_action(rotate_action_);
  modify_group->add_action(mirror_action_);
  modify_group->add_action(array_action_);
  modify_group->add_action(fillet_action_);
  modify_group->add_action(chamfer_action_);

  // 注释：放文字（派生标注的显示开关在「视图 → Annotations」那一组）。
  RibbonGroup* annotate_group =
      home_page->add_group(QStringLiteral("annotate"), tr("Annotate"));
  annotate_group->add_action(text_action_);

  RibbonGroup* navigation_group =
      home_page->add_group(QStringLiteral("navigation"), tr("Navigation"));
  navigation_group->add_action(frame_all_action_);

  RibbonGroup* setting_group =
      home_page->add_group(QStringLiteral("settings"), tr("Settings"));
  setting_group->add_action(settings_action_);

  RibbonGroup* plugins_group =
      home_page->add_group(QStringLiteral("plugins"), tr("Plugins"));
  plugins_group->add_action(manage_action_);

  RibbonGroup* help_group = home_page->add_group(QStringLiteral("help"), tr("Help"));
  help_group->add_action(about_action_);

  RibbonPage* view_page = ribbon->add_page(QStringLiteral("view"), tr("View"));
  RibbonGroup* display_ribbon =
      view_page->add_group(QStringLiteral("display"), tr("Display"));
  display_ribbon->add_action(wireframe_action_);
  display_ribbon->add_action(shaded_action_);
  display_ribbon->add_action(realistic_action_);
  display_ribbon->add_action(xray_action_);
  display_ribbon->add_action(grid_action_);
  display_ribbon->add_action(grid_settings_action_);
  display_ribbon->add_action(drawing_visible_action_);

  // 标注归一组：都是「派生文字」，关掉只是这一帧不画，不改文档。
  RibbonGroup* labels_ribbon =
      view_page->add_group(QStringLiteral("labels"), tr("Annotations"));
  labels_ribbon->add_action(label_axis_action_);
  labels_ribbon->add_action(label_level_action_);
  labels_ribbon->add_action(label_dimension_action_);

  RibbonGroup* panels_group = view_page->add_group(QStringLiteral("panels"), tr("Panels"));
  // 构件显隐面板住在视口右上角的工具面板里（不在停靠区），这里只给入口与快捷键。
  components_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/components.svg")), tr("Components"), this);
  components_action_->setShortcut(QKeySequence(tr("Ctrl+L")));
  components_action_->setToolTip(tr("Show or hide components by category"));
  connect(components_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->toggle_visibility_panel();
    }
  });
  addAction(components_action_);
  panels_group->add_action(components_action_);
  // 楼层面板同样住在视口右侧的工具列里（同一列的第二个功能页）。
  floors_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/storey.svg")), tr("Floors"), this);
  floors_action_->setShortcut(QKeySequence(tr("Ctrl+Shift+L")));
  floors_action_->setToolTip(tr("Show or hide floors, and set floor heights"));
  connect(floors_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->toggle_floor_panel();
    }
  });
  addAction(floors_action_);
  panels_group->add_action(floors_action_);
  // 楼层管理：同一列里的第三个功能页，把楼层当视图清单用（双击打开某层视图）。
  floor_views_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/floor_manager.svg")), tr("Floor Views"),
                  this);
  floor_views_action_->setToolTip(
      tr("List the global 3D view and every floor; double-click one to open it"));
  connect(floor_views_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->toggle_floor_manager_panel();
    }
  });
  addAction(floor_views_action_);
  panels_group->add_action(floor_views_action_);
  // 图纸管理：住在视口右侧工具列里的功能页（和构件显隐 / 楼层同一列）。
  drawings_action_ =
      new QAction(ribbon_icon(QStringLiteral(":/icons/drawing.svg")), tr("Drawings"), this);
  drawings_action_->setToolTip(
      tr("Manage reference drawings (DWF / DWFx / DXF / PDF…): add, show/hide, place, "
         "or open in a 2D page"));
  connect(drawings_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->toggle_drawing_panel();
    }
  });
  addAction(drawings_action_);
  panels_group->add_action(drawings_action_);
  panels_group->add_action(draw_toggle_);
  panels_group->add_action(property_toggle_);
  panels_group->add_action(texture_toggle_);
  panels_group->add_action(handle_toggle_);
  panels_group->add_action(diagnostics_action_);  // 图形诊断也是「面板」类工具
  panels_group->add_action(debug_scene_action_);
  panels_group->add_action(timing_toggle_);
  panels_group->add_action(console_toggle_);

  RibbonGroup* workspace_group =
      view_page->add_group(QStringLiteral("workspace"), tr("Workspace"));
  workspace_group->add_action(home_action_);

  // 最上面那行菜单（文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助）。功能区在这行下面，
  // 和 FreeCAD 一样：菜单管"找得到"，图标行和功能区管"够得着"。
  //
  // 注意上面两条 add_page：页签已经去掉了，开始 / 视图 这两页现在是同一条工具带上的
  // 两段分区（左边竖排分区名 + 主色条，见 RibbonBar::refresh_section_chrome），
  // 不再需要切页签。页 id（home / view）保留着，布局记忆与插件落点还按它认。
  build_menu_bar();

  plugin_host_.set_log_sink([this](std::string_view msg) {
    const QString text = QString::fromUtf8(msg.data(), static_cast<int>(msg.size()));
    statusBar()->showMessage(text, 8000);
    if (console_panel_ != nullptr) {
      console_panel_->append_line(text);  // 插件 / 脚本的输出也落在同一个控制台
    }
  });
  plugin_host_.set_dialog_handler(
      [this](std::int32_t kind, std::int32_t buttons, std::string_view spec, std::string& out) {
        return show_plugin_dialog(this, kind, buttons, spec, out);
      });
  {
    // 扩展的约定位置：内置的在 exe 旁边（随版本发布），用户装的在 <AppData>/extensions
    // ——和脚本目录同一层。先建出来，用户才知道往哪放。
    const QString app_data = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString user_root = QDir(app_data).filePath(QStringLiteral("extensions"));
    QDir().mkpath(user_root);
    std::vector<std::filesystem::path> roots;
    roots.push_back(executable_directory() / "plugins");
    roots.push_back(qstring_to_path(user_root));
    log_info("Extension roots: " + path_to_utf8(roots[0]) + " | " + path_to_utf8(roots[1]));
    plugin_host_.set_extension_roots(std::move(roots));
  }
  if (auto loaded = plugin_host_.load(); !loaded) {
    log_warn(loaded.error());
    statusBar()->showMessage(QString::fromStdString(loaded.error()), 8000);
  }
  {
    std::unordered_set<std::string> disabled;
    for (const QString& id : AppSettings::instance().disabled_plugin_ids()) {
      disabled.insert(id.toStdString());
    }
    plugin_manager_.set_disabled_ids(std::move(disabled));
    std::vector<std::string> order;
    for (const QString& id : AppSettings::instance().ribbon_command_order()) {
      order.push_back(id.toStdString());
    }
    plugin_manager_.set_command_order(std::move(order));
  }
  rebuild_plugin_ribbon(ribbon);

  // 扩展目录的文件监视：目录里有动静就重扫，只重载内容真的变了的那些。
  // 盯盘在 Qt 事件循环里（C++ 侧），重载本身在托管侧跑。
  extension_watcher_ = new ExtensionWatcher(plugin_host_.extension_roots(), this);
  connect(extension_watcher_, &ExtensionWatcher::changed, this, [this, ribbon] {
    const auto summary = plugin_host_.reload_extensions();
    if (!summary) {
      log_warn(summary.error());
      console_panel_->append_line(QString::fromStdString(summary.error()),
                                  ConsolePanel::LineStyle::Error);
      return;
    }
    if (summary->empty()) {
      return;  // 目录里有动静，但扩展本身没变（编辑器临时文件、隔壁文件之类）
    }
    rebuild_plugin_ribbon(ribbon);  // 命令表变了，按钮跟着重建
    const QString text = QString::fromStdString(*summary);
    console_panel_->append_line(text);
    statusBar()->showMessage(text, 8000);
    // loader 可能用 LoadExtension 声明了新的根（那些工程不在约定目录里），
    // 换掉监视表再挂：目录可能新增或消失。
    extension_watcher_->set_roots(plugin_host_.extension_roots());
  });

  // 分组布局：把上次的排 / 排内位置原样摆回来。先在这之前记下「代码里的默认布局」，
  // 这样「重置布局」（Ribbon 样式菜单里）有东西可回。
  ribbon->remember_default_layout();
  {
    auto& settings = AppSettings::instance();
    if (!ribbon->apply_layout(settings.ribbon_layout())) {
      // 没有记录（首次运行）或记录全对不上：丢掉它，别留着一份永远用不上的旧布局。
      settings.set_ribbon_layout({});
    }
  }

  // 上次被拖出去、还漂在外面的那几组工具：按记住的位置恢复。
  for (const QString& entry : AppSettings::instance().ribbon_floating_groups()) {
    const QStringList fields = entry.split(QLatin1Char('|'));
    if (fields.size() != 4) {
      continue;
    }
    bool x_ok = false;
    bool y_ok = false;
    const int x = fields[2].toInt(&x_ok);
    const int y = fields[3].toInt(&y_ok);
    if (!x_ok || !y_ok) {
      continue;
    }
    ribbon->restore_floating_group(fields[0], fields[1], QPoint(x, y));
  }

  setMenuWidget(ribbon);
  // 卷起 / 展开的状态也记着（放在 setMenuWidget 之后：这时 Ribbon 才真正进了窗口）
  ribbon->set_collapsed(AppSettings::instance().ribbon_collapsed());

  // 面板停靠布局（属性 / 绘制 / 贴图 / 句柄 / 计时 / 控制台）：上次拖到哪儿、多大、
  // 是不是浮着、关掉没有，一起还原。
  {
    const QByteArray dock_state = AppSettings::instance().window_state();
    if (!dock_state.isEmpty()) {
      restoreState(dock_state);
    }
  }
  // 从那以后：布局一动就（节流）记下来，不用等到关窗口。
  window_state_timer_ = new QTimer(this);
  window_state_timer_->setSingleShot(true);
  window_state_timer_->setInterval(400);
  connect(window_state_timer_, &QTimer::timeout, this, &MainWindow::persist_window_state);
  installEventFilter(this);
  window_state_ready_ = true;

  connect(property_panel_, &PropertyPanel::param_edited, this,
          [this](std::uint64_t entity_id, std::uint64_t feature_id, const QString& param_name,
                 double value) {
            if (auto* vp = current_viewport()) {
              vp->set_entity_param(entity_id, feature_id, param_name.toStdString(), value);
            }
          });
  connect(property_panel_, &PropertyPanel::material_edited, this,
          [this](std::uint64_t entity_id, Material material) {
            if (auto* vp = current_viewport()) {
              vp->set_entity_material(entity_id, std::move(material));
            }
          });
  connect(property_panel_, &PropertyPanel::material_shared_updated, this,
          [this](Material material) {
            if (auto* vp = current_viewport()) {
              vp->update_library_material(material);
            }
          });
  connect(property_panel_, &PropertyPanel::texture_import_requested, this,
          [this](quint64 target_id, TextureAsset asset, int slot, bool edit_shared) {
            auto* vp = current_viewport();
            if (vp == nullptr) {
              return;
            }
            Document& doc = vp->document();
            const std::uint64_t tid = vp->import_texture(std::move(asset));
            if (tid == 0) {
              return;
            }
            Material material{};
            const Entity* entity = doc.entity(target_id);
            const SceneNode* node = doc.scene().find(target_id);
            const std::uint64_t mid =
                entity != nullptr ? entity->material_id : (node != nullptr ? node->material_id : 0);
            if (mid != 0) {
              if (const Material* m = doc.material(mid)) {
                material = *m;
              }
            }
            if (slot == 1) {
              material.normal_texture_id = tid;
            } else if (slot == 2) {
              material.orm_texture_id = tid;
            } else {
              material.albedo_texture_id = tid;
            }
            if (edit_shared && material.id != 0) {
              vp->update_library_material(material);
            } else {
              material.id = 0;
              material.name.clear();
              vp->set_entity_material(target_id, material);
            }
            refresh_property_panel();
          });
  connect(texture_library_panel_, &TextureLibraryPanel::texture_import_requested, this,
          [this](TextureAsset asset) {
            if (auto* vp = current_viewport()) {
              vp->import_texture(std::move(asset));
            }
          });
  connect(texture_library_panel_, &TextureLibraryPanel::texture_replace_requested, this,
          [this](quint64 id, TextureAsset asset) {
            if (auto* vp = current_viewport()) {
              vp->replace_texture(id, std::move(asset));
            }
          });
  connect(property_panel_, &PropertyPanel::location_edited, this,
          [this](std::uint64_t entity_id, std::uint64_t storey_id,
                 double elevation_offset) {
            if (auto* vp = current_viewport()) {
              vp->set_entity_location(entity_id, storey_id, elevation_offset);
            }
          });
  refresh_property_panel();
  refresh_handle_inspector();
  refresh_texture_library_panel();

  statusBar()->showMessage(tr("Ready — Open a model"));
  show_home();
}

MainWindow::~MainWindow() {
  if (tabs_ != nullptr) {
    for (int i = 0; i < tabs_->count(); ++i) {
      if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(i))) {
        vp->cancel_plugin_point_input();
      }
    }
  }
  plugin_host_.shutdown();
}

void MainWindow::showEvent(QShowEvent* event) {
  QMainWindow::showEvent(event);
  if (placed_on_primary_) {
    return;
  }
  center_on_primary_screen(this);
  placed_on_primary_ = true;
}

void MainWindow::set_create_tool(ToolMode mode) {
  auto* vp = current_viewport();
  if (vp == nullptr) {
    // 没有模型文档（起始页 / 参考图纸页）时构件工具无处落地，也不该弹面板。
    sync_create_tool_actions(ToolMode::None);
    sync_draw_panel();
    statusBar()->showMessage(tr("Open a document to draw components"), 5000);
    return;
  }
  // 同一构件已经武装：只把面板拉回前台，别重设工具——set_tool 会取消视口里的 pending，
  // 让"再点一次图标"变成静默取消绘制。
  const bool keep_armed = draw_panel_ != nullptr && draw_panel_->is_armed() &&
                          draw_panel_->current_mode() == mode;
  if (!keep_armed) {
    if (draw_panel_ != nullptr) {
      // 先换面板：set_component 可能 emit disarmed（换构件 = 放弃上一个 pending），
      // 顺序反了会把刚设好的工具又取消掉。
      draw_panel_->set_component(mode);
    }
    vp->set_tool(mode);
    sync_create_tool_actions(vp->tool_mode());
  }
  // 只有真正带规格的构件才需要这块面板；草图工具（直线/圆/多段线…）点了就画，
  // 弹出一张占位表单只会添乱。
  const bool has_component = draw_panel_ != nullptr &&
                             draw_panel_->current_mode() == mode &&
                             draw_panel_->has_component();
  if (has_component && draw_dock_ != nullptr) {
    draw_dock_->show();
    draw_dock_->raise();
  }
}

void MainWindow::sync_draw_panel() {
  DocumentViewport* vp = current_viewport();
  const bool has_document = vp != nullptr;
  if (draw_toggle_ != nullptr) {
    draw_toggle_->setEnabled(has_document);
    draw_toggle_->setToolTip(has_document ? tr("Show the draw settings panel")
                                          : tr("Open a document to draw components"));
  }
  if (draw_panel_ == nullptr || draw_dock_ == nullptr) {
    return;
  }
  if (!has_document) {
    // 文档关闭后绘制设置没有意义：收起面板、清掉构件选择与武装状态。
    draw_dock_->hide();
    draw_panel_->set_component(ToolMode::None);
    sync_create_tool_actions(ToolMode::None);
    return;
  }
  // 工具状态跟着活跃文档走（和 sync_render_mode_actions 一个道理）：切到别的文档时
  // 功能区高亮必须反映那个文档自己的工具，而不是上一个文档留下的。
  sync_create_tool_actions(vp->tool_mode());
  // 面板武装状态属于具体文档：切到工具不同的文档时按钮要回到"开始绘制"，
  // 否则会留在"结束绘制"却没有任何 pending 命令的假状态。
  if (draw_panel_->is_armed() && draw_panel_->current_mode() != vp->tool_mode()) {
    draw_panel_->clear_armed();
  }
}

// 切楼层（楼层管理双击 / 楼层面板换当前楼层 / 改标高 / 改层高）以后，绘制面板上
// 「标高偏移」这类默认值也要跟着换：板默认画本层顶，默认值是**当前**楼层的层高。
// 值真变了就按新参数重新武装，视口里那条 pending 命令才不会停在旧楼层的标高上。
void MainWindow::refresh_draw_panel_storey_defaults() {
  if (draw_panel_ == nullptr || !draw_panel_->has_component()) {
    return;
  }
  if (!draw_panel_->refresh_storey_defaults()) {
    return;
  }
  if (draw_panel_->is_armed()) {
    draw_panel_->rearm();
  }
}

void MainWindow::sync_create_tool_actions(ToolMode mode) {
  if (!create_group_) {
    return;
  }
  const bool was_exclusive = create_group_->isExclusive();
  create_group_->setExclusive(false);
  const auto actions = create_group_->actions();
  for (QAction* action : actions) {
    action->setChecked(false);
  }
  if (mode != ToolMode::None) {
    const int expected = static_cast<int>(mode);
    for (QAction* action : actions) {
      if (action->property("toolMode").toInt() == expected) {
        action->setChecked(true);
        break;
      }
    }
  }
  create_group_->setExclusive(was_exclusive);
}

void MainWindow::show_home() {
  refresh_home();
  stack_->setCurrentWidget(home_);
  sync_draw_panel();
  sync_document_actions();
}

void MainWindow::show_documents() {
  if (tabs_->count() == 0) {
    show_home();
    return;
  }
  stack_->setCurrentWidget(tabs_);
  // addTab emits currentChanged before the stack leaves the home page, so
  // current_viewport() is still null and the texture library would stay empty.
  refresh_property_panel();
  refresh_handle_inspector();
  refresh_texture_library_panel();
  sync_draw_panel();
  sync_document_actions();
}

void MainWindow::activate_open_document(int index) {
  if (index < 0 || index >= tabs_->count()) {
    return;
  }
  tabs_->setCurrentIndex(index);
  show_documents();
}

void MainWindow::refresh_home() {
  home_->refresh(recent_.items());
  QVector<OpenDocumentItem> open_items;
  open_items.reserve(tabs_->count());
  for (int i = 0; i < tabs_->count(); ++i) {
    OpenDocumentItem item;
    item.index = i;
    item.name = tabs_->tabText(i);
    if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(i))) {
      const auto& path = vp->document().path();
      if (!path.empty()) {
        item.path = path_to_qstring(path);
      }
    }
    open_items.push_back(item);
  }
  home_->set_open_documents(open_items);
}

int MainWindow::find_open_document(const QString& path) const {
  if (path.isEmpty()) {
    return -1;
  }
  const QString abs = QFileInfo(path).absoluteFilePath();
  for (int i = 0; i < tabs_->count(); ++i) {
    if (auto* drawing = qobject_cast<DrawingView*>(tabs_->widget(i))) {
      const QString& drawing_path = drawing->document().path();
      if (!drawing_path.isEmpty() &&
          QFileInfo(drawing_path).absoluteFilePath() == abs) {
        return i;
      }
      continue;
    }
    auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(i));
    if (!vp) {
      continue;
    }
    const auto& doc_path = vp->document().path();
    if (doc_path.empty()) {
      continue;
    }
    if (QFileInfo(path_to_qstring(doc_path)).absoluteFilePath() == abs) {
      return i;
    }
  }
  return -1;
}

Result<void> MainWindow::populate_document_meshes(
    Document& document, RenderThread& thread,
    const UiLoadProgressCallback& progress) {
  const int mesh_count = static_cast<int>(document.meshes().size());
  int uploaded = 0;
  for (auto& [asset_id, asset] : document.meshes()) {
    if (progress && mesh_count > 0) {
      progress(uploaded * 100 / mesh_count,
               tr("Uploading geometry (%1 / %2)…")
                   .arg(uploaded + 1)
                   .arg(mesh_count));
    }
    auto gpu_id = thread.upload_mesh(asset_id, asset.cpu);
    if (!gpu_id) {
      return Err(gpu_id.error());
    }
    ++uploaded;
  }
  if (progress) {
    progress(100, tr("Preparing the scene…"));
  }
  document.recompute_scene();
  return {};
}

void MainWindow::open_about() {
  AboutDialog dialog(this);
  dialog.exec();
}

void MainWindow::open_graphics_diagnostics() {
  GraphicsDiagnosticsDialog dialog(this);
  dialog.exec();
}

void MainWindow::open_settings() {
  SettingsDialog dialog(this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  QStringList notes;
  if (dialog.language_changed()) {
    notes << tr("Language changes take effect after restarting Tamias.");
  }
  if (dialog.backend_changed()) {
    notes << tr("Render backend changes take effect after restarting Tamias.");
  }
  if (dialog.ribbon_style_changed() && ribbon_ != nullptr) {
    ribbon_->set_display_mode(AppSettings::instance().ribbon_style() == QStringLiteral("icons")
                                  ? RibbonDisplayMode::IconOnly
                                  : RibbonDisplayMode::IconWithText);
  }
  if (!notes.isEmpty()) {
    QMessageBox::information(this, tr("Settings"), notes.join(QStringLiteral("\n\n")));
  }
}

void MainWindow::open_plugin_manager() {
  PluginManagerDialog dialog(plugin_manager_, this);
  if (dialog.exec() != QDialog::Accepted) {
    return;
  }
  plugin_manager_.commit_disabled_among_loaded(
      dialog.disabled_loaded_ids());
  plugin_manager_.commit_command_order_among_loaded(
      dialog.ordered_command_ids());
  QStringList stored;
  stored.reserve(static_cast<int>(plugin_manager_.disabled_ids().size()));
  for (const auto& id : plugin_manager_.disabled_ids()) {
    stored.push_back(QString::fromStdString(id));
  }
  stored.sort();
  AppSettings::instance().set_disabled_plugin_ids(stored);
  QStringList command_order;
  command_order.reserve(
      static_cast<int>(plugin_manager_.command_order().size()));
  for (const std::string& id : plugin_manager_.command_order()) {
    command_order.push_back(QString::fromStdString(id));
  }
  AppSettings::instance().set_ribbon_command_order(command_order);
  AppSettings::instance().save();
  apply_plugin_visibility();
  apply_plugin_order();
}

void MainWindow::apply_plugin_visibility() {
  for (const auto& item : plugin_ribbon_buttons_) {
    const bool visible = plugin_manager_.is_enabled(item.plugin_id);
    if (!visible && item.action != nullptr && item.action->isChecked()) {
      if (auto* viewport = current_viewport()) {
        viewport->cancel_plugin_point_input();
      }
      item.action->setChecked(false);
    }
    if (item.button != nullptr) {
      item.button->setVisible(visible);
    }
  }
}

void MainWindow::apply_plugin_order() {
  std::unordered_map<RibbonGroup*, std::pair<std::string, std::string>>
      locations;
  for (const auto& item : plugin_ribbon_buttons_) {
    if (item.group != nullptr) {
      locations.emplace(item.group,
                        std::pair{item.page_id, item.group_id});
    }
  }
  for (const auto& [group, location] : locations) {
    std::unordered_map<std::string, QToolButton*> buttons;
    for (const auto& item : plugin_ribbon_buttons_) {
      if (item.group == group && item.button != nullptr) {
        buttons.emplace(item.command_id, item.button);
      }
    }
    std::vector<QToolButton*> ordered;
    for (const PluginCommand* command :
         plugin_manager_.ordered_commands(location.first, location.second)) {
      if (const auto it = buttons.find(command->id); it != buttons.end()) {
        ordered.push_back(it->second);
      }
    }
    group->reorder_buttons(ordered);
  }
}

// 按当前的插件命令表重建 Ribbon 上的插件按钮。
// 启动时调一次；扩展重载之后还要再调（命令表变了，旧按钮不能留着）。
void MainWindow::rebuild_plugin_ribbon(RibbonBar* ribbon) {
  for (auto& item : plugin_ribbon_buttons_) {
    // 按钮析构会自己从分组布局里摘掉；动作得单独删。
    delete item.button;
    delete item.action;
  }
  plugin_ribbon_buttons_.clear();

  std::vector<const PluginCommand*> plugin_commands;
  plugin_commands.reserve(plugin_host_.commands().size());
  for (const auto& cmd : plugin_host_.commands()) {
    plugin_commands.push_back(&cmd);
  }
  std::unordered_map<std::string, std::size_t> command_rank;
  for (std::size_t i = 0; i < plugin_manager_.command_order().size(); ++i) {
    command_rank.emplace(plugin_manager_.command_order()[i], i);
  }
  std::stable_sort(
      plugin_commands.begin(), plugin_commands.end(),
      [&command_rank](const PluginCommand* a, const PluginCommand* b) {
        const auto ar = command_rank.find(a->id);
        const auto br = command_rank.find(b->id);
        if (ar != command_rank.end() || br != command_rank.end()) {
          if (ar == command_rank.end()) {
            return false;
          }
          if (br == command_rank.end()) {
            return true;
          }
          return ar->second < br->second;
        }
        if (a->placement.order != b->placement.order) {
          return a->placement.order < b->placement.order;
        }
        return a->id < b->id;
      });
  for (const PluginCommand* command : plugin_commands) {
    const PluginCommand& cmd = *command;
    const QString page_id = QString::fromStdString(cmd.placement.page_id);
    const QString group_id = QString::fromStdString(cmd.placement.group_id);
    RibbonPage* target_page = ribbon->find_page(page_id);
    if (target_page == nullptr) {
      target_page = ribbon->add_page(page_id, page_id);
    }
    RibbonGroup* target_group = target_page->find_group(group_id);
    if (target_group == nullptr) {
      target_group = target_page->add_group(group_id, group_id);
    }
    const QString icon_path = cmd.placement.icon_path.empty()
                                  ? QStringLiteral(":/icons/inspector.svg")
                                  : QString::fromStdString(cmd.placement.icon_path);
    auto* action = new QAction(ribbon_icon(icon_path),
                               QString::fromUtf8(cmd.title.data(), static_cast<int>(cmd.title.size())),
                               this);
    action->setCheckable(cmd.placement.checkable);
    if (!cmd.tooltip.empty()) {
      action->setToolTip(
          QString::fromUtf8(cmd.tooltip.data(), static_cast<int>(cmd.tooltip.size())));
    }
    const std::string id = cmd.id;
    connect(action, &QAction::triggered, this, [this, id, action](bool checked) {
      if (!plugin_manager_.is_command_enabled(id)) {
        action->setChecked(false);
        statusBar()->showMessage(tr("This plugin is disabled."), 4000);
        return;
      }
      bind_plugin_session();
      if (action->isCheckable() && !checked) {
        if (auto* vp = current_viewport()) {
          vp->cancel_plugin_point_input();
        }
        return;
      }
      if (auto r = plugin_host_.invoke(id); !r) {
        action->setChecked(false);
        statusBar()->showMessage(QString::fromStdString(r.error()), 5000);
        log_error(r.error());
      }
    });
    PluginRibbonButton item;
    item.command_id = cmd.id;
    item.plugin_id = cmd.plugin_id;
    item.page_id = cmd.placement.page_id;
    item.group_id = cmd.placement.group_id;
    item.group = target_group;
    item.button = target_group->add_action(action);
    item.action = action;
    plugin_ribbon_buttons_.push_back(item);
  }
  apply_plugin_visibility();
  apply_plugin_order();
}

void MainWindow::add_document_tab(std::shared_ptr<Document> document,
                                  const ViewportState* viewport,
                                  const UiLoadProgressCallback& progress) {
  const RenderDeviceConfig config = AppSettings::instance().render_device_config();
  auto thread = RenderThreadPool::instance().acquire(config);
  if (!thread) {
    QMessageBox::critical(
        this, tr("Render"),
        tr("Failed to create %1 render thread.")
            .arg(QString::fromUtf8(to_string(config.backend))));
    return;
  }
  if (auto r = populate_document_meshes(*document, *thread, progress); !r) {
    QMessageBox::critical(this, tr("Upload"), QString::fromStdString(r.error()));
    return;
  }
  // Parent after addTab so the first present sees a real laid-out size. Applying
  // viewport (and redrawing) before addTab can create a tiny swapchain that only
  // fills the top-left corner of the window.
  auto* vp = new DocumentViewport(document, thread, nullptr);
  connect(vp, &DocumentViewport::tool_mode_changed, this, [this](ToolMode mode) {
    sync_create_tool_actions(mode);
    if (draw_panel_ != nullptr) {
      // 工具退出（Esc / 右键 / 切换）时取消面板武装状态。
      if (mode == ToolMode::None) {
        draw_panel_->set_armed(false);
      }
    }
  });
  // 视口自己打开轴网时（轴网布柱看不见轴就没得框）Ribbon 的勾选也跟着走。
  connect(vp, &DocumentViewport::grid_visible_changed, this, [this](bool visible) {
    if (grid_action_ == nullptr) {
      return;
    }
    const QSignalBlocker block(grid_action_);
    grid_action_->setChecked(visible);
  });
  connect(vp, &DocumentViewport::status_message, this, [this](const QString& text) {
    statusBar()->showMessage(text, 5000);
  });
  connect(vp, &DocumentViewport::console_message, this, [this](const QString& text) {
    if (console_panel_ != nullptr) {
      console_panel_->append_line(text);
    }
  });
  connect(vp, &DocumentViewport::selection_changed, this, &MainWindow::refresh_property_panel);
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::refresh_property_panel);
  // 撤销 / 重做有没有得撤，命令执行完才准——菜单栏和顶栏那两个图标一起跟着灰 / 亮。
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::sync_document_actions);
  connect(vp, &DocumentViewport::selection_changed, this, &MainWindow::refresh_handle_inspector);
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::refresh_handle_inspector);
  connect(vp, &DocumentViewport::document_changed, this,
          &MainWindow::refresh_texture_library_panel);
  // 楼层默认值只对当前文档生效：后台页签的文档变了不该去动前台这张面板。
  connect(vp, &DocumentViewport::document_changed, this, [this, vp] {
    if (current_viewport() == vp) {
      refresh_draw_panel_storey_defaults();
    }
  });
  connect(vp, &DocumentViewport::drawing_open_requested, this,
          [this](const QString& path) { open_drawing_tab(path); });
  connect(vp, &DocumentViewport::selection_changed, this,
          &MainWindow::refresh_texture_library_panel);
  connect(vp, &DocumentViewport::plugin_point_input_changed, this,
          [this](bool active) {
            if (active) {
              return;
            }
            for (const auto& item : plugin_ribbon_buttons_) {
              if (item.action != nullptr && item.action->isCheckable()) {
                item.action->setChecked(false);
              }
            }
          });
  const int index = tabs_->addTab(vp, QString::fromStdString(document->name()));
  tabs_->setCurrentIndex(index);
  show_documents();
  if (viewport) {
    vp->apply_viewport_state(*viewport);
    vp->request_redraw();
  }
  sync_render_mode_actions();
  sync_bim_actions();
  bind_plugin_session();
  // add_entity / add_import_mesh mark dirty while assembling the initial scene.
  // That baseline is not a user edit, so closing without further changes must
  // not prompt to save.
  document->clear_dirty();
}

void MainWindow::new_document() {
  auto document = std::make_shared<Document>(tr("Untitled").toStdString());
  add_document_tab(document);
}

bool MainWindow::open_path(const QString& path) {
  if (path.isEmpty()) {
    return false;
  }
  if (const int existing = find_open_document(path); existing >= 0) {
    activate_open_document(existing);
    return true;
  }
  const QFileInfo info(path);
  if (!info.exists()) {
    QMessageBox::warning(this, tr("Open"), tr("File not found:\n%1").arg(path));
    return false;
  }

  const auto file = qstring_to_path(info.absoluteFilePath());
  OpenProgressDialog progress(this, tr("Open Project"),
                              tr("Opening %1…").arg(info.fileName()));
  progress.stage(3, tr("Opening %1…").arg(info.fileName()));

  if (DrawingDocument::is_drawing_path(path)) {
    progress.busy(tr("Reading drawing %1…").arg(info.fileName()));
    // 有打开的模型就挂到它下面、在视口里当底图看（图纸管理面板负责显隐与摆放）；
    // 没有模型（例如从开始页打开）时退回只读的二维页签。
    if (!open_drawing_in_viewport(info.absoluteFilePath())) {
      open_drawing_tab(info.absoluteFilePath());
      progress.stage(100, tr("Drawing opened."));
      return true;
    }
    progress.stage(100, tr("Drawing attached."));
    return true;
  }

  if (is_render_scene_path(file)) {
    progress.busy(tr("Reading render scene %1…").arg(info.fileName()));
    auto loaded = load_render_scene(file);
    if (!loaded) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(loaded.error()));
      return false;
    }
    progress.stage(82, tr("Generating preview…"));
    QString thumb_path;
    if (!loaded->meshes.empty()) {
      const QImage thumb = render_mesh_thumbnail(loaded->meshes.begin()->second);
      thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
    }
    progress.stage(94, tr("Opening render scene…"));
    open_scene_debugger(std::move(*loaded), file, nullptr);
    recent_.add(info.absoluteFilePath(), thumb_path);
    refresh_home();
    progress.stage(100, tr("Render scene opened."));
    statusBar()->showMessage(tr("Opened render scene in debugger: %1").arg(info.absoluteFilePath()),
                             5000);
    return true;
  }

  if (is_tdoc_document_path(file)) {
    auto loaded = load_document(file, [&progress, &info](float fraction) {
      progress.stage(5 + static_cast<int>(fraction * 60.0f),
                     tr("Reading project %1…").arg(info.fileName()));
    });
    if (!loaded) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(loaded.error()));
      return false;
    }
    ViewportState vp_storage = loaded->viewport;
    const bool has_viewport = loaded->has_viewport;
    auto document = std::make_shared<Document>(std::move(loaded->document));
    // Prefer the on-disk filename so tabs/recent show .tdoc even if an older
    // file still has an imported .obj name in META.
    document->set_path(file);
    document->set_name(path_to_utf8(file.filename()));
    progress.stage(68, tr("Uploading geometry…"));
    add_document_tab(document, has_viewport ? &vp_storage : nullptr,
                     [&progress](int percent, const QString& label) {
                       progress.stage(68 + percent * 24 / 100, label);
                     });

    progress.stage(94, tr("Generating preview…"));
    const MeshCpu* thumb_mesh = nullptr;
    if (!document->meshes().empty()) {
      thumb_mesh = &document->meshes().begin()->second.cpu;
    }
    if (thumb_mesh) {
      const QImage thumb = render_mesh_thumbnail(*thumb_mesh);
      const QString thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
      recent_.add(info.absoluteFilePath(), thumb_path);
    } else {
      recent_.add(info.absoluteFilePath(), QString());
    }
    refresh_home();
    progress.stage(100, tr("Project opened."));
    statusBar()->showMessage(tr("Loaded %1").arg(info.absoluteFilePath()), 5000);
    return true;
  }

  if (info.suffix().compare(QStringLiteral("ifc"), Qt::CaseInsensitive) == 0) {
    progress.busy(tr("Parsing IFC structure %1…").arg(info.fileName()));
    auto tree = format_ifc_spatial_tree(file);
    if (!tree) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(tree.error()));
      return false;
    }
    progress.close();
    QDialog dlg(this);
    dlg.setWindowTitle(tr("IFC spatial structure"));
    dlg.resize(640, 480);
    auto* layout = new QVBoxLayout(&dlg);
    auto* edit = new QPlainTextEdit(&dlg);
    edit->setReadOnly(true);
    edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit->setPlainText(QString::fromStdString(*tree));
    layout->addWidget(edit);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    layout->addWidget(buttons);
    dlg.exec();
    statusBar()->showMessage(
        tr("Parsed %1 (geometry import not yet)").arg(info.absoluteFilePath()), 5000);
    return true;
  }

  std::unique_ptr<Shape> cad_shape;
  std::optional<ImportedModel> imported;
  if (occt_supports_extension(file)) {
    progress.busy(tr("Reading CAD geometry %1…").arg(info.fileName()));
    TAMIAS_TIMING_SCOPE("open_file", TimingCategory::Command);
    auto* ops = ShapeOpsRegistry::instance().find("occt");
    if (!ops) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), tr("OCCT ShapeOps is not registered."));
      return false;
    }
    auto shape = ops->read_file(file);
    if (!shape) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(shape.error()));
      return false;
    }
    cad_shape = std::move(*shape);
    progress.stage(45, tr("Preparing CAD geometry…"));
  } else {
    progress.busy(tr("Reading mesh %1…").arg(info.fileName()));
    auto model = load_mesh_model(file);
    if (!model) {
      progress.close();
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(model.error()));
      return false;
    }
    imported = std::move(*model);
    progress.stage(45, tr("Preparing mesh materials…"));
  }
  auto document = std::make_shared<Document>(path_to_utf8(file.filename()));
  document->set_path(file);
  std::uint64_t mesh_id = 0;
  if (cad_shape) {
    mesh_id = document->add_import_shape(path_to_utf8(file.filename()), std::move(cad_shape),
                                         Mat4::identity(), {0.75f, 0.78f, 0.82f});
  } else if (imported) {
    auto import_slot = [&](const std::string& path, const std::vector<std::uint8_t>& bytes,
                           TextureUsage usage, bool srgb) -> std::uint64_t {
      Result<TextureAsset> asset = Err("none");
      if (!bytes.empty()) {
        const QByteArray raw(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<int>(bytes.size()));
        std::string name = "imported";
        if (!path.empty()) {
          name = std::filesystem::path(path).stem().string();
        }
        asset = decode_texture_image(raw, std::move(name), usage, srgb);
      } else if (!path.empty()) {
        asset = load_texture_image(QString::fromStdString(path), usage, srgb);
      } else {
        return 0;
      }
      if (!asset) {
        return 0;
      }
      return document->import_texture(std::move(*asset)).id;
    };
    Material mat{};
    mat.name = path_to_utf8(file.stem());
    mat.base_color = imported->base_color;
    mat.roughness = imported->roughness;
    mat.metallic = imported->metallic;
    mat.opacity = imported->opacity;
    mat.albedo_texture_id =
        import_slot(imported->albedo_path, imported->albedo_bytes, TextureUsage::Albedo, true);
    mat.normal_texture_id =
        import_slot(imported->normal_path, imported->normal_bytes, TextureUsage::Normal, false);
    mat.orm_texture_id =
        import_slot(imported->orm_path, imported->orm_bytes, TextureUsage::Orm, false);
    const std::uint64_t mat_id = document->add_material(std::move(mat)).id;
    const bool has_colors = mesh_has_vertex_colors(imported->mesh);
    const Vec3 color = has_colors ? Vec3{1.f, 1.f, 1.f} : imported->base_color;
    mesh_id = document->add_import_mesh(path_to_utf8(file.filename()), std::move(imported->mesh),
                                        Mat4::identity(), color, mat_id);
  }
  if (mesh_id == 0) {
    progress.close();
    QMessageBox::critical(this, tr("Open"), tr("Failed to add imported geometry."));
    return false;
  }
  add_document_tab(document, nullptr, [&progress](int percent, const QString& label) {
    progress.stage(55 + percent * 35 / 100, label);
  });

  progress.stage(94, tr("Generating preview…"));
  const MeshAsset* asset = document->mesh(mesh_id);
  MeshCpu thumb_mesh;
  if (asset != nullptr && !asset->cpu.vertices.empty()) {
    thumb_mesh = asset->cpu;
  } else if (asset != nullptr && asset->cpu.bounds.valid()) {
    const Vec3 e = asset->cpu.bounds.extent();
    thumb_mesh = make_box_mesh(std::max(e.x, 0.01f), std::max(e.y, 0.01f), std::max(e.z, 0.01f));
  } else {
    thumb_mesh = make_box_mesh(1.f, 1.f, 1.f);
  }
  const QImage thumb = render_mesh_thumbnail(thumb_mesh);
  const QString thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
  recent_.add(info.absoluteFilePath(), thumb_path);
  refresh_home();
  progress.stage(100, tr("File opened."));
  statusBar()->showMessage(tr("Loaded %1").arg(info.absoluteFilePath()), 5000);
  return true;
}

void MainWindow::open_file() {
  const QString filters =
      tr("All Supported (*.tdoc *.trscn *.gltf *.glb *.obj *.step *.stp *.iges *.igs *.brep *.ifc "
         "*.pdf *.dxf *.svg *.png *.jpg *.jpeg *.bmp *.tif *.tiff);;"
         "Tamias (*.tdoc);;"
         "Render Scene (*.trscn);;"
         "Meshes (*.gltf *.glb *.obj);;"
         "CAD (*.step *.stp *.iges *.igs *.brep);;"
         "IFC (*.ifc);;"
         "Drawings (*.pdf *.dxf *.svg *.png *.jpg *.jpeg *.bmp *.tif *.tiff);;"
         "glTF (*.gltf *.glb);;OBJ (*.obj);;"
         "STEP (*.step *.stp);;IGES (*.iges *.igs);;BREP (*.brep)");
  const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), filters);
  if (path.isEmpty()) {
    return;
  }
  open_path(path);
}

void MainWindow::open_paths(const QStringList& paths) {
  for (const QString& path : paths) {
    if (path.isEmpty() || path.startsWith(QLatin1Char('-'))) {
      continue;
    }
    open_path(path);
  }
}

void MainWindow::open_drawing_file() {
  const QString path =
      QFileDialog::getOpenFileName(this, tr("Open Drawing"), QString(),
                                   DrawingDocument::file_dialog_filter());
  if (path.isEmpty()) {
    return;
  }
  const QString absolute = QFileInfo(path).absoluteFilePath();
  if (!open_drawing_in_viewport(absolute)) {
    open_drawing_tab(absolute);
  }
}

DocumentViewport* MainWindow::drawing_target_viewport() const {
  if (DocumentViewport* current = current_viewport()) {
    return current;
  }
  for (int i = 0; i < tabs_->count(); ++i) {
    if (auto* viewport = qobject_cast<DocumentViewport*>(tabs_->widget(i))) {
      return viewport;
    }
  }
  return nullptr;
}

// 打开图纸的新方式：挂到文档下、画在文档视口里（模型之下），而不是另开一个二维视口。
// 没有打开的文档才退回二维页签。已经挂过的图纸不再重复挂，直接把它框出来。
bool MainWindow::open_drawing_in_viewport(const QString& path) {
  DocumentViewport* viewport = drawing_target_viewport();
  if (viewport == nullptr) {
    return false;
  }
  const std::string key = QFileInfo(path).absoluteFilePath().toStdString();
  tabs_->setCurrentWidget(viewport);
  if (viewport->document().drawing(key) != nullptr) {
    viewport->set_drawings_visible(true);
    viewport->frame_drawing(key);
    statusBar()->showMessage(
        tr("%1 is already attached — framed it in the viewport.").arg(QFileInfo(path).fileName()),
        6000);
    return true;
  }
  viewport->add_document_drawings({key});
  viewport->set_drawings_visible(true);
  viewport->set_drawing_panel_open(true);
  if (drawing_visible_action_ != nullptr) {
    const QSignalBlocker block(drawing_visible_action_);
    drawing_visible_action_->setChecked(true);
  }
  statusBar()->showMessage(
      tr("Attached %1 — it is drawn under the model (Drawings panel: show/hide, scale, "
         "position, 2D page)")
          .arg(QFileInfo(path).fileName()),
      9000);
  return true;
}

void MainWindow::open_drawing_tab(const QString& path) {
  if (path.isEmpty()) {
    return;
  }
  if (const int existing = find_open_document(path); existing >= 0) {
    activate_open_document(existing);
    return;
  }
  QString error;
  std::unique_ptr<DrawingDocument> document = DrawingDocument::open(path, error);
  if (!document) {
    QMessageBox::critical(this, tr("Open Drawing"), error);
    return;
  }
  const QString title = document->title();
  const QString detail = document->detail_text();
  auto* view = new DrawingView(std::move(document), nullptr);
  connect(view, &DrawingView::status_message, this, [this](const QString& text) {
    statusBar()->showMessage(text, 4000);
  });
  const int index = tabs_->addTab(view, title);
  tabs_->setCurrentIndex(index);
  show_documents();
  view->setFocus();
  view->fit_to_window();

  const QImage thumb = view->document().render_thumbnail(QSize(320, 180));
  const QString thumb_path = save_mesh_thumbnail(path, thumb);
  recent_.add(path, thumb_path);
  refresh_home();
  statusBar()->showMessage(
      tr("Opened drawing %1 (%2) — wheel to zoom, drag to pan, F to fit").arg(title, detail),
      8000);
}

bool MainWindow::is_obj_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0;
}

bool MainWindow::is_tdoc_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("tdoc"), Qt::CaseInsensitive) == 0;
}

bool MainWindow::is_trscn_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("trscn"), Qt::CaseInsensitive) == 0;
}

const MeshCpu* MainWindow::selected_mesh(Document& document) const {
  const MeshAsset* asset = document.selected_mesh();
  return asset ? &asset->cpu : nullptr;
}

const MeshCpu* MainWindow::mesh_for_obj_export(Document& document) const {
  if (const MeshCpu* selected = selected_mesh(document)) {
    return selected;
  }
  if (document.meshes().empty()) {
    return nullptr;
  }
  return &document.meshes().begin()->second.cpu;
}

bool MainWindow::write_selected_mesh(const QString& path) {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }
  Document& document = vp->document();
  const MeshCpu* mesh = mesh_for_obj_export(document);
  if (!mesh) {
    QMessageBox::information(this, tr("Save"), tr("The document has no mesh to export."));
    return false;
  }

  QString out_path = path;
  if (!is_obj_path(out_path)) {
    out_path += QStringLiteral(".obj");
  }
  const QString abs_path = QFileInfo(out_path).absoluteFilePath();
  const auto file = qstring_to_path(abs_path);
  if (auto r = save_mesh_file(file, *mesh); !r) {
    QMessageBox::critical(this, tr("Save"), QString::fromStdString(r.error()));
    return false;
  }

  document.set_path(file);
  document.set_name(path_to_utf8(file.filename()));
  if (const int index = tabs_->indexOf(vp); index >= 0) {
    tabs_->setTabText(index, QString::fromStdString(document.name()));
  }

  const QImage thumb = render_mesh_thumbnail(*mesh);
  const QString thumb_path = save_mesh_thumbnail(abs_path, thumb);
  recent_.add(abs_path, thumb_path);
  refresh_home();
  notify_save_success(abs_path);
  return true;
}

bool MainWindow::export_render_scene() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Render Scene"), tr("Open a document first."));
    return false;
  }
  QString suggested = QString::fromStdString(vp->document().name());
  if (suggested.isEmpty()) {
    suggested = QStringLiteral("scene");
  }
  if (!is_trscn_path(suggested)) {
    suggested = QFileInfo(suggested).completeBaseName() + QStringLiteral(".trscn");
  }
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save Render Scene Snapshot"), suggested, tr("Render Scene (*.trscn)"));
  if (path.isEmpty()) {
    return false;
  }
  QString out_path = path;
  if (!is_trscn_path(out_path)) {
    out_path += QStringLiteral(".trscn");
  }
  const QString abs_path = QFileInfo(out_path).absoluteFilePath();
  const RenderScene scene = vp->capture_debug_scene();
  if (auto r = save_render_scene(qstring_to_path(abs_path), scene); !r) {
    QMessageBox::critical(this, tr("Render Scene"), QString::fromStdString(r.error()));
    return false;
  }
  if (auto r = write_render_scene_debug_sidecars(qstring_to_path(abs_path), scene); !r) {
    QMessageBox::warning(this, tr("Render Scene"),
                         tr("Scene saved, but debug dump failed:\n%1")
                             .arg(QString::fromStdString(r.error())));
  }
  statusBar()->showMessage(
      tr("Saved snapshot %1  digest=%2")
          .arg(abs_path, QString::fromStdString(render_scene_digest(scene))),
      8000);
  reveal_path(qstring_to_path(QFileInfo(abs_path).absolutePath()));
  return true;
}

bool MainWindow::pin_render_scene_golden() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Pin"), tr("Open a document first."));
    return false;
  }

  const std::filesystem::path source_dir{TAMIAS_SOURCE_DIR};
  const auto root = render_scene_golden_root(source_dir);
  if (!std::filesystem::exists(source_dir)) {
    QMessageBox::warning(this, tr("Pin"),
                         tr("Source tree not found. Pin is for a local checkout:\n%1")
                             .arg(QString::fromUtf8(TAMIAS_SOURCE_DIR)));
    return false;
  }

  QString suggested = QString::fromStdString(
      suggest_render_scene_golden_slug(vp->document().name()));
  bool ok = false;
  const QString slug_q = QInputDialog::getText(
      this, tr("Pin Render Scene for Tests"),
      tr("Fixture name (letters, digits, '-' '_'; saved under assets/samples/render/):"),
      QLineEdit::Normal, suggested, &ok);
  if (!ok || slug_q.trimmed().isEmpty()) {
    return false;
  }
  const std::string slug = slug_q.trimmed().toStdString();
  if (!is_render_scene_golden_slug(slug)) {
    QMessageBox::warning(
        this, tr("Pin"),
        tr("Name must start with a letter and use only A–Z, a–z, 0–9, '-' or '_'."));
    return false;
  }

  const auto dir = root / slug;
  bool overwrite = false;
  std::error_code ec;
  if (std::filesystem::exists(dir / "scene.trscn", ec)) {
    const auto answer = QMessageBox::question(
        this, tr("Pin"),
        tr("Golden \"%1\" already exists.\nOverwrite scene.trscn and sidecar files?")
            .arg(QString::fromStdString(slug)),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) {
      return false;
    }
    overwrite = true;
  }

  const RenderScene scene = vp->capture_debug_scene();
  auto meta = save_render_scene_golden(root, slug, scene, overwrite);
  if (!meta) {
    QMessageBox::critical(this, tr("Pin"), QString::fromStdString(meta.error()));
    return false;
  }

  const QString dir_q = path_to_qstring(dir);

  const GoldenTestRun tests = run_render_scene_golden_tests(source_dir, this);

  QString inspect = tr("Full dump: %1\nMeshes / textures: %2\n\n")
                        .arg(path_to_qstring(dir / "scene.inspect.txt"),
                             path_to_qstring(dir / "debug"));
  inspect += QStringLiteral("--- RenderSceneGolden* ---\n");
  inspect += tests.log;

  if (tests.ok) {
    statusBar()->showMessage(
        tr("Pinned golden %1  digest=%2").arg(dir_q, QString::fromStdString(meta->digest)), 8000);
  } else {
    statusBar()->showMessage(tr("Pinned %1 but RenderSceneGolden* failed").arg(dir_q), 12000);
  }
  PinResultDialog dlg(scene, dir_q, inspect, tests, this);
  dlg.exec();
  return true;
}

bool MainWindow::write_render_scene_document(const QString& path, bool show_inspect) {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Export"), tr("Open a document first."));
    return false;
  }
  QString out_path = path;
  if (!is_trscn_path(out_path)) {
    out_path += QStringLiteral(".trscn");
  }
  const QString abs_path = QFileInfo(out_path).absoluteFilePath();
  const auto file = qstring_to_path(abs_path);
  Document& document = vp->document();
  const RenderScene scene = vp->capture_debug_scene();
  if (auto r = save_render_scene(file, scene); !r) {
    QMessageBox::critical(this, show_inspect ? tr("Export") : tr("Save"),
                          QString::fromStdString(r.error()));
    return false;
  }
  if (auto r = write_render_scene_debug_sidecars(file, scene); !r) {
    QMessageBox::warning(this, show_inspect ? tr("Export") : tr("Save"),
                         tr("Scene saved, but debug dump failed:\n%1")
                             .arg(QString::fromStdString(r.error())));
  }
  document.set_path(file);
  document.set_name(path_to_utf8(file.filename()));
  document.set_render_snapshot(scene);
  document.clear_dirty();
  if (const int index = tabs_->indexOf(vp); index >= 0) {
    tabs_->setTabText(index, QString::fromStdString(document.name()));
  }
  statusBar()->showMessage(tr("Wrote render scene: %1").arg(abs_path), 8000);
  if (!show_inspect) {
    notify_save_success(abs_path);
    return true;
  }

  QDialog dlg(this);
  dlg.setWindowTitle(tr("Render scene exported"));
  dlg.resize(640, 400);
  auto* layout = new QVBoxLayout(&dlg);
  auto* edit = new QPlainTextEdit(&dlg);
  edit->setReadOnly(true);
  edit->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  edit->setPlainText(QString::fromStdString(inspect_render_scene(scene)));
  layout->addWidget(edit);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dlg);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
  layout->addWidget(buttons);
  dlg.exec();
  return true;
}

bool MainWindow::write_tdoc_document(const QString& path) {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }
  Document& document = vp->document();
  QString out_path = path;
  if (!is_tdoc_path(out_path)) {
    out_path += QStringLiteral(".tdoc");
  }
  const QString abs_path = QFileInfo(out_path).absoluteFilePath();
  const auto file = qstring_to_path(abs_path);
  // Update identity before serialize so META stores the .tdoc name, not the
  // imported .obj/.step source name.
  document.set_path(file);
  document.set_name(path_to_utf8(file.filename()));
  const ViewportState viewport = vp->capture_viewport_state();
  if (auto r = save_document(file, document, viewport); !r) {
    QMessageBox::critical(this, tr("Save"), QString::fromStdString(r.error()));
    return false;
  }

  document.clear_dirty();
  if (const int index = tabs_->indexOf(vp); index >= 0) {
    tabs_->setTabText(index, QString::fromStdString(document.name()));
  }

  const MeshCpu* thumb_mesh = selected_mesh(document);
  if (!thumb_mesh && !document.meshes().empty()) {
    thumb_mesh = &document.meshes().begin()->second.cpu;
  }
  if (thumb_mesh) {
    const QImage thumb = render_mesh_thumbnail(*thumb_mesh);
    const QString thumb_path = save_mesh_thumbnail(abs_path, thumb);
    recent_.add(abs_path, thumb_path);
  } else {
    recent_.add(abs_path, QString());
  }
  refresh_home();
  notify_save_success(abs_path);
  return true;
}

void MainWindow::notify_save_success(const QString& path) {
  // 保存成功只飘一条提示：不弹窗、不用点「确定」，几秒后自己消失。
  Toast::show_message(stack_, tr("Saved successfully: %1").arg(path), ToastLevel::Info);
}

bool MainWindow::save_file() {
  auto* vp = current_viewport();
  if (!vp) {
    if (current_drawing_view() != nullptr) {
      // 图纸是只读参考底图，没有可回写的内容。
      Toast::show_message(stack_, tr("Reference drawings are read-only — nothing to save."),
                          ToastLevel::Warning);
      return false;
    }
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }

  // Save .tdoc in place. Opened .trscn snapshots write back as render scenes.
  // Imported .obj/.step paths are not overwritten — prompt Save As.
  const auto& doc_path = vp->document().path();
  if (!doc_path.empty() && is_tdoc_path(path_to_qstring(doc_path))) {
    return write_tdoc_document(path_to_qstring(doc_path));
  }
  if (!doc_path.empty() && is_trscn_path(path_to_qstring(doc_path))) {
    return write_render_scene_document(path_to_qstring(doc_path), false);
  }
  return save_file_as();
}

bool MainWindow::save_file_as() {
  auto* vp = current_viewport();
  if (!vp) {
    if (current_drawing_view() != nullptr) {
      Toast::show_message(stack_, tr("Reference drawings are read-only — nothing to save."),
                          ToastLevel::Warning);
      return false;
    }
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }

  QString suggested;
  const auto& doc_path = vp->document().path();
  const bool snapshot = vp->document().render_snapshot() != nullptr;
  if (!doc_path.empty()) {
    QFileInfo info(path_to_qstring(doc_path));
    const QString ext = snapshot || is_trscn_path(info.fileName())
                            ? QStringLiteral(".trscn")
                            : QStringLiteral(".tdoc");
    suggested = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + ext;
  } else {
    suggested = QString::fromStdString(vp->document().name());
    if (snapshot) {
      if (!is_trscn_path(suggested)) {
        suggested = QFileInfo(suggested).completeBaseName() + QStringLiteral(".trscn");
      }
    } else if (!is_tdoc_path(suggested) && !is_obj_path(suggested)) {
      suggested += QStringLiteral(".tdoc");
    } else if (is_obj_path(suggested)) {
      suggested = QFileInfo(suggested).completeBaseName() + QStringLiteral(".tdoc");
    }
  }

  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save As"), suggested,
      tr("Tamias Document (*.tdoc);;Render Scene (*.trscn);;OBJ Mesh Export (*.obj)"));
  if (path.isEmpty()) {
    return false;
  }
  if (is_obj_path(path) ||
      (!is_tdoc_path(path) && path.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive))) {
    return write_selected_mesh(path);
  }
  if (is_trscn_path(path)) {
    return write_render_scene_document(path, false);
  }
  return write_tdoc_document(path);
}

void MainWindow::open_recent_path(const QString& path) { open_path(path); }

void MainWindow::on_missing_recent(const QString& path) {
  const auto answer = QMessageBox::question(
      this, tr("Missing file"),
      tr("This file no longer exists:\n%1\n\nRemove it from Recent?").arg(path),
      QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
  if (answer == QMessageBox::Yes) {
    recent_.remove(path);
    refresh_home();
  }
}

DocumentViewport* MainWindow::current_viewport() const {
  if (stack_->currentWidget() != tabs_) {
    return nullptr;
  }
  return qobject_cast<DocumentViewport*>(tabs_->currentWidget());
}

DrawingView* MainWindow::current_drawing_view() const {
  if (stack_->currentWidget() != tabs_) {
    return nullptr;
  }
  return qobject_cast<DrawingView*>(tabs_->currentWidget());
}

void MainWindow::refresh_property_panel() {
  if (property_panel_ == nullptr) {
    return;
  }
  DocumentViewport* vp = current_viewport();
  if (vp == nullptr) {
    property_panel_->show_entity(nullptr, nullptr, tr("No document open"));
    return;
  }
  Document& doc = vp->document();
  if (const Entity* entity = doc.selected_entity()) {
    property_panel_->show_entity(entity, &doc, QString());
    return;
  }
  if (const SceneNode* node = doc.scene().selected_node()) {
    property_panel_->show_imported_mesh(node, &doc);
    return;
  }
  property_panel_->show_entity(
      nullptr, nullptr, tr("No selection\nClick an object to select it, or use a create tool"));
}

void MainWindow::refresh_handle_inspector() {
  if (handle_inspector_ == nullptr) {
    return;
  }
  DocumentViewport* vp = current_viewport();
  if (vp == nullptr) {
    handle_inspector_->show_selection(nullptr, 0);
    return;
  }
  Document& doc = vp->document();
  const SceneNode* node = doc.scene().selected_node();
  handle_inspector_->show_selection(&doc, node ? node->id : 0);
}

void MainWindow::refresh_texture_library_panel() {
  if (texture_library_panel_ == nullptr) {
    return;
  }
  DocumentViewport* vp = current_viewport();
  texture_library_panel_->set_document(vp != nullptr ? &vp->document() : nullptr);
}

void MainWindow::ensure_scene_debugger() {
  if (scene_debugger_ != nullptr) {
    return;
  }
  const RenderDeviceConfig config = AppSettings::instance().render_device_config();
  scene_debugger_ = new SceneDebuggerWindow(RenderThreadPool::instance().acquire(config), this);
}

void MainWindow::open_scene_debugger(RenderScene scene, const std::filesystem::path& path,
                                     DocumentViewport* source) {
  ensure_scene_debugger();
  scene_debugger_->open_scene(std::move(scene), path, source);
}

void MainWindow::debug_current_frame() {
  auto* vp = current_viewport();
  if (vp == nullptr) {
    QMessageBox::information(this, tr("Scene Debugger"), tr("Open a document first."));
    return;
  }
  open_scene_debugger(vp->capture_debug_scene(), {}, vp);
}

void MainWindow::frame_all() {
  if (auto* vp = current_viewport()) {
    vp->frame_scene();
    return;
  }
  if (auto* drawing = current_drawing_view()) {
    drawing->fit_to_window();
  }
}

void MainWindow::set_render_mode(RenderMode mode) {
  if (auto* vp = current_viewport()) {
    vp->set_render_mode(mode);
  }
  sync_render_mode_actions();
}

void MainWindow::sync_render_mode_actions() {
  if (!wireframe_action_ || !shaded_action_ || !realistic_action_) {
    return;
  }
  RenderMode mode = RenderMode::Shaded;
  bool xray = false;
  auto* vp = current_viewport();
  if (vp != nullptr) {
    mode = vp->render_mode();
    xray = vp->xray();
  }
  const QSignalBlocker b0(wireframe_action_);
  const QSignalBlocker b1(shaded_action_);
  const QSignalBlocker b2(realistic_action_);
  wireframe_action_->setChecked(mode == RenderMode::Wireframe);
  shaded_action_->setChecked(mode == RenderMode::Shaded);
  realistic_action_->setChecked(mode == RenderMode::Realistic);
  if (xray_action_ != nullptr) {
    // xray 是独立开关（不在互斥的 display_group 里）：切文档时要反映那个文档
    // 自己的状态，而不是上一个文档留下的。
    const QSignalBlocker b3(xray_action_);
    xray_action_->setEnabled(vp != nullptr);
    xray_action_->setChecked(xray);
  }
}

// 轴网与翻模的勾选/可用状态跟着活跃文档走（和渲染模式一个道理）：切标签页时按钮要
// 反映那个文档自己的状态，而不是上一个文档留下的。
void MainWindow::sync_bim_actions() {
  auto* vp = current_viewport();
  if (grid_action_ != nullptr && grid_settings_action_ != nullptr) {
    const QSignalBlocker block(grid_action_);
    grid_action_->setEnabled(vp != nullptr);
    grid_action_->setChecked(vp == nullptr || vp->grid_visible());
    grid_settings_action_->setEnabled(vp != nullptr);
  }
  if (trace_drawing_action_ != nullptr) {
    trace_drawing_action_->setEnabled(vp != nullptr);
  }
  if (drawing_visible_action_ != nullptr) {
    // 底图总开关跟着活跃文档走（和轴网一个道理）：切页签要反映那个文档的状态。
    const QSignalBlocker block(drawing_visible_action_);
    drawing_visible_action_->setEnabled(vp != nullptr);
    drawing_visible_action_->setChecked(vp == nullptr || vp->drawings_visible());
  }
  // 标注开关同样跟着活跃文档走。
  const auto sync_label_action = [vp](QAction* action, TextKind kind) {
    if (action == nullptr) {
      return;
    }
    const QSignalBlocker block(action);
    action->setEnabled(vp != nullptr);
    action->setChecked(vp == nullptr || vp->label_kind_visible(kind));
  };
  sync_label_action(label_axis_action_, TextKind::AxisLabel);
  sync_label_action(label_level_action_, TextKind::StoreyLabel);
  sync_label_action(label_dimension_action_, TextKind::Dimension);
}

void MainWindow::bind_plugin_session() {
  auto* vp = current_viewport();
  if (vp == nullptr) {
    plugin_host_.unbind();
    plugin_host_.set_point_input_handlers({}, {});
    plugin_host_.set_selection_changed({});
    return;
  }
  plugin_host_.bind(&vp->document(), &vp->command_system(), [vp] { vp->refresh_after_edit(); });
  plugin_host_.set_selection_changed([vp] { vp->notify_selection_changed(); });
  plugin_host_.set_point_input_handlers(
      [vp](PluginPointInputRequest request,
           PluginHost::PointInputCompletion completion) {
        return vp->begin_plugin_point_input(std::move(request),
                                            std::move(completion));
      },
      [vp](std::uint64_t request_id) {
        vp->cancel_plugin_point_input(request_id);
      });
}

void MainWindow::activate_viewport(DocumentViewport* vp) {
  if (vp == nullptr) {
    return;
  }
  const int index = tabs_->indexOf(vp);
  if (index < 0) {
    return;
  }
  tabs_->setCurrentIndex(index);
  show_documents();
}

bool MainWindow::confirm_close_document(DocumentViewport* vp) {
  if (vp == nullptr || !vp->document().dirty()) {
    return true;
  }

  const QString name = QString::fromStdString(vp->document().name());
  QMessageBox box(this);
  box.setIcon(QMessageBox::Warning);
  box.setWindowTitle(tr("Unsaved changes"));
  box.setText(tr("Do you want to save changes to \"%1\"?").arg(name));
  QAbstractButton* save_btn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
  QAbstractButton* discard_btn =
      box.addButton(tr("Don't Save"), QMessageBox::DestructiveRole);
  QAbstractButton* cancel_btn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
  box.setDefaultButton(qobject_cast<QPushButton*>(save_btn));
  box.setEscapeButton(cancel_btn);
  box.exec();

  if (box.clickedButton() == save_btn) {
    activate_viewport(vp);
    if (!save_file()) {
      return false;
    }
    // OBJ export does not clear dirty; keep the tab open so the project
    // is not silently discarded after an incomplete save.
    return !vp->document().dirty();
  }
  if (box.clickedButton() == discard_btn) {
    return true;
  }
  return false;
}

void MainWindow::closeEvent(QCloseEvent* event) {
  for (int i = 0; i < tabs_->count(); ++i) {
    auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(i));
    if (!confirm_close_document(vp)) {
      event->ignore();
      return;
    }
  }
  persist_window_state();  // 面板布局：关窗前再存一次，别指望那点节流时间
  QMainWindow::closeEvent(event);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  // QMainWindow 没有「停靠布局变了」的信号；面板挪动 / 改大小 / 显隐都会让这一层
  // 重新布局，拿 LayoutRequest 当触发点，再用定时器把连续的一串收敛成一次存盘。
  if (watched == this && event->type() == QEvent::LayoutRequest && window_state_ready_ &&
      window_state_timer_ != nullptr) {
    window_state_timer_->start();
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::persist_window_state() {
  if (!window_state_ready_) {
    return;
  }
  auto& settings = AppSettings::instance();
  settings.set_window_state(saveState());
  settings.save();
}

void MainWindow::close_tab(int index) {
  if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(index))) {
    if (!confirm_close_document(vp)) {
      return;
    }
  }
  if (auto* w = tabs_->widget(index)) {
    tabs_->removeTab(index);
    delete w;
  }
  if (tabs_->count() == 0) {
    show_home();
  }
}

}  // namespace tamias
