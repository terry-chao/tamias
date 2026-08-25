#include "main_window.h"

#include "about_dialog.h"
#include "app_settings.h"
#include "bim/ifc_spatial_tree.h"
#include "engine/core/log.h"
#include "engine/document/document_io.h"
#include "entity/box_entity.h"
#include "engine/io/mesh_io.h"
#include "mesh_thumbnail.h"
#include "engine/modeling/occt_shape_ops.h"
#include "engine/modeling/shape_ops.h"
#include "handle_inspector.h"
#include "plugin/plugin_host.h"
#include "plugin/plugin_manager.h"
#include "plugin_manager_dialog.h"
#include "property_panel.h"
#include "ribbon_bar.h"
#include "ribbon_group.h"
#include "ribbon_page.h"
#include "settings_dialog.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QShowEvent>
#include <QStyle>
#include <QStatusBar>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QAbstractButton>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QSignalBlocker>
#include <QSize>
#include <QStatusBar>
#include <QToolButton>
#include <QVector>
#include <QVBoxLayout>
#include <QWidget>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace tamias {
namespace {

QIcon themed_mask_icon(const QString& resource, const QColor& color) {
  const QIcon source(resource);
  QIcon result;
  for (int extent : {16, 32}) {
    for (int scale = 1; scale <= 2; ++scale) {
      const int px = extent * scale;
      QPixmap canvas(px, px);
      canvas.setDevicePixelRatio(scale);
      canvas.fill(Qt::transparent);
      {
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        source.paint(&painter, QRect(0, 0, extent, extent));
        painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
        painter.fillRect(QRect(0, 0, extent, extent), color);
      }
      result.addPixmap(canvas);
    }
  }
  return result;
}

QIcon ribbon_icon(const QString& resource) {
  return themed_mask_icon(resource, QColor(47, 125, 222));
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

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), plugin_manager_(plugin_host_) {
  setWindowTitle("Tamias");
  setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));
  resize(1600, 1000);
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
    refresh_property_panel();
    refresh_handle_inspector();
    bind_plugin_session();
  });
  connect(home_, &HomePage::openRequested, this, &MainWindow::open_file);
  connect(home_, &HomePage::fileActivated, this, &MainWindow::open_recent_path);
  connect(home_, &HomePage::missingFileActivated, this, &MainWindow::on_missing_recent);
  connect(home_, &HomePage::recentRemoveRequested, this, [this](const QString& path) {
    recent_.remove(path);
    refresh_home();
  });
  connect(home_, &HomePage::openDocumentActivated, this, &MainWindow::activate_open_document);
  connect(home_, &HomePage::settingsRequested, this, &MainWindow::open_settings);

  auto* new_action = new QAction(ribbon_icon(QStringLiteral(":/icons/new.svg")),
                                 tr("New"), this);
  new_action->setShortcut(QKeySequence::New);
  new_action->setToolTip(tr("New document with a cube"));
  connect(new_action, &QAction::triggered, this, &MainWindow::new_document);
  addAction(new_action);

  auto* open_action = new QAction(ribbon_icon(QStringLiteral(":/icons/open.svg")),
                                  tr("Open"), this);
  open_action->setShortcut(QKeySequence::Open);
  open_action->setToolTip(tr("Open a model file"));
  connect(open_action, &QAction::triggered, this, &MainWindow::open_file);
  addAction(open_action);

  auto* save_action = new QAction(ribbon_icon(QStringLiteral(":/icons/save.svg")),
                                  tr("Save"), this);
  save_action->setShortcut(QKeySequence::Save);
  save_action->setToolTip(tr("Save the document"));
  connect(save_action, &QAction::triggered, this, &MainWindow::save_file);
  addAction(save_action);

  auto* save_as_action = new QAction(ribbon_icon(QStringLiteral(":/icons/save_as.svg")),
                                    tr("Save As"), this);
  save_as_action->setShortcut(QKeySequence::SaveAs);
  save_as_action->setToolTip(tr("Save the document to a new file"));
  connect(save_as_action, &QAction::triggered, this, &MainWindow::save_file_as);
  addAction(save_as_action);

  auto* frame_all_action = new QAction(ribbon_icon(QStringLiteral(":/icons/frame_all.svg")),
                                      tr("Fit All"), this);
  frame_all_action->setShortcut(QKeySequence(tr("F")));
  frame_all_action->setToolTip(tr("Frame all geometry in the view"));
  connect(frame_all_action, &QAction::triggered, this, &MainWindow::frame_all);
  addAction(frame_all_action);

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

  box_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/box.svg")),
                           tr("Box"), this);
  box_action_->setCheckable(true);
  box_action_->setProperty("toolMode", static_cast<int>(ToolMode::Box));
  box_action_->setToolTip(tr("Create a box: click to place"));
  connect(box_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Box); });
  create_group_->addAction(box_action_);
  addAction(box_action_);

  cylinder_action_ = new QAction(ribbon_icon(QStringLiteral(":/icons/cylinder.svg")),
                                tr("Cylinder"), this);
  cylinder_action_->setCheckable(true);
  cylinder_action_->setProperty("toolMode", static_cast<int>(ToolMode::Cylinder));
  cylinder_action_->setToolTip(tr("Create a cylinder: click to place"));
  connect(cylinder_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Cylinder); });
  create_group_->addAction(cylinder_action_);
  addAction(cylinder_action_);

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

  auto* settings_action = new QAction(ribbon_icon(QStringLiteral(":/icons/settings.svg")),
                                     tr("Settings"), this);
  settings_action->setShortcut(QKeySequence(tr("Ctrl+,")));
  settings_action->setToolTip(tr("Open settings"));
  connect(settings_action, &QAction::triggered, this, &MainWindow::open_settings);
  addAction(settings_action);

  auto* about_action = new QAction(ribbon_icon(QStringLiteral(":/icons/about.svg")),
                                   tr("About"), this);
  about_action->setToolTip(tr("About Tamias"));
  connect(about_action, &QAction::triggered, this, &MainWindow::open_about);
  addAction(about_action);

  auto* home_action = new QAction(ribbon_icon(QStringLiteral(":/icons/home.svg")),
                                 tr("Welcome"), this);
  home_action->setToolTip(tr("Back to the welcome page"));
  connect(home_action, &QAction::triggered, this, &MainWindow::show_home);
  addAction(home_action);

  auto* undo_action = new QAction(ribbon_icon(QStringLiteral(":/icons/undo.svg")),
                                 tr("Undo"), this);
  undo_action->setShortcut(QKeySequence::Undo);
  connect(undo_action, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->undo();
    }
  });
  addAction(undo_action);

  auto* redo_action = new QAction(ribbon_icon(QStringLiteral(":/icons/redo.svg")),
                                 tr("Redo"), this);
  redo_action->setShortcut(QKeySequence::Redo);
  connect(redo_action, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->redo();
    }
  });
  addAction(redo_action);

  auto* exit_action = new QAction(tr("E&xit"), this);
  exit_action->setShortcut(QKeySequence::Quit);
  connect(exit_action, &QAction::triggered, this, &QWidget::close);
  addAction(exit_action);

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

  // 右侧属性面板：展示/编辑选中实体的参数。
  property_panel_ = new PropertyPanel(this);
  auto* property_dock = new QDockWidget(tr("Properties"), this);
  property_dock->setObjectName(QStringLiteral("propertyDock"));
  property_dock->setWidget(property_panel_);
  property_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, property_dock);
  QAction* property_toggle = property_dock->toggleViewAction();
  property_toggle->setIcon(ribbon_icon(QStringLiteral(":/icons/properties.svg")));
  addAction(property_toggle);

  handle_inspector_ = new HandleInspector(this);
  auto* handle_dock = new QDockWidget(tr("Handle Inspector"), this);
  handle_dock->setObjectName(QStringLiteral("handleInspectorDock"));
  handle_dock->setWidget(handle_inspector_);
  handle_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, handle_dock);
  handle_dock->hide();
  auto* handle_toggle = handle_dock->toggleViewAction();
  handle_toggle->setText(tr("Inspector"));
  handle_toggle->setIcon(ribbon_icon(QStringLiteral(":/icons/inspector.svg")));
  handle_toggle->setShortcut(QKeySequence(tr("Ctrl+D")));
  handle_toggle->setToolTip(tr("Inspect the selected component's document handle"));
  addAction(handle_toggle);

  auto* ribbon = new RibbonBar(this);
  ribbon->add_quick_action(undo_action);
  ribbon->add_quick_action(redo_action);

  RibbonPage* home_page = ribbon->add_page(QStringLiteral("home"), tr("Home"));
  RibbonGroup* file_group = home_page->add_group(QStringLiteral("file"), tr("File"));
  file_group->add_action(new_action);
  file_group->add_action(open_action);
  file_group->add_action(save_action);
  file_group->add_action(save_as_action);

  RibbonGroup* draw_group = home_page->add_group(QStringLiteral("draw"), tr("Draw"));
  draw_group->add_action(line_action_);
  draw_group->add_action(polyline_action_);
  draw_group->add_action(rectangle_action_);
  draw_group->add_action(circle_action_);
  draw_group->add_action(arc_action_);
  draw_group->add_action(bezier_action_);
  draw_group->add_action(bspline_action_);

  RibbonGroup* primitives_group =
      home_page->add_group(QStringLiteral("primitives"), tr("Primitives"));
  primitives_group->add_action(box_action_);
  primitives_group->add_action(cylinder_action_);

  RibbonGroup* building_group =
      home_page->add_group(QStringLiteral("building"), tr("Building"));
  building_group->add_action(wall_action_);
  building_group->add_action(beam_action_);
  building_group->add_action(column_action_);
  building_group->add_action(slab_action_);
  building_group->add_action(door_action_);
  building_group->add_action(window_action_);

  RibbonGroup* modify_group = home_page->add_group(QStringLiteral("modify"), tr("Modify"));
  modify_group->add_action(fillet_action_);
  modify_group->add_action(chamfer_action_);

  RibbonGroup* navigation_group =
      home_page->add_group(QStringLiteral("navigation"), tr("Navigation"));
  navigation_group->add_action(frame_all_action);

  RibbonGroup* setting_group =
      home_page->add_group(QStringLiteral("settings"), tr("Settings"));
  setting_group->add_action(settings_action);

  RibbonGroup* help_group = home_page->add_group(QStringLiteral("help"), tr("Help"));
  help_group->add_action(about_action);

  RibbonPage* view_page = ribbon->add_page(QStringLiteral("view"), tr("View"));
  RibbonGroup* display_ribbon =
      view_page->add_group(QStringLiteral("display"), tr("Display"));
  display_ribbon->add_action(wireframe_action_);
  display_ribbon->add_action(shaded_action_);
  display_ribbon->add_action(realistic_action_);

  RibbonGroup* panels_group = view_page->add_group(QStringLiteral("panels"), tr("Panels"));
  panels_group->add_action(property_toggle);
  panels_group->add_action(handle_toggle);

  RibbonGroup* workspace_group =
      view_page->add_group(QStringLiteral("workspace"), tr("Workspace"));
  workspace_group->add_action(home_action);

  plugin_host_.set_log_sink([this](std::string_view msg) {
    statusBar()->showMessage(QString::fromUtf8(msg.data(), static_cast<int>(msg.size())), 8000);
  });
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
  RibbonPage* plugins_page = ribbon->add_page(QStringLiteral("plugins"), tr("Plugins"));
  RibbonGroup* manage_group =
      plugins_page->add_group(QStringLiteral("manage"), tr("Manage"));
  auto* manage_action = new QAction(ribbon_icon(QStringLiteral(":/icons/settings.svg")),
                                    tr("Plugin Manager"), this);
  manage_action->setToolTip(tr("Choose which loaded plugins appear on the ribbon"));
  connect(manage_action, &QAction::triggered, this, &MainWindow::open_plugin_manager);
  manage_group->add_action(manage_action);

  plugin_commands_group_ =
      plugins_page->add_group(QStringLiteral("commands"), tr("Commands"));
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
    item.in_default_group = target_group == plugin_commands_group_;
    plugin_ribbon_buttons_.push_back(item);
  }
  apply_plugin_visibility();
  apply_plugin_order();

  setMenuWidget(ribbon);

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
  connect(property_panel_, &PropertyPanel::location_edited, this,
          [this](std::uint64_t entity_id, std::uint64_t storey_id,
                 double elevation_offset) {
            if (auto* vp = current_viewport()) {
              vp->set_entity_location(entity_id, storey_id, elevation_offset);
            }
          });
  refresh_property_panel();
  refresh_handle_inspector();

  statusBar()->showMessage(tr("Ready — Open a model"));
  show_home();

  // ===== TEMP DEBUG: auto-create a concrete box to diagnose black texture =====
  {
    auto doc = std::make_shared<Document>(tr("Untitled").toStdString());
    BoxEntity box(Vec3{0.0f, 0.0f, 0.0f});
    auto geom = box.createGeom();
    if (geom) {
      Entity* e = doc->add_entity(std::make_unique<BoxEntity>(std::move(box)),
                                  std::move(*geom));
      e->material_id = 2;  // Concrete (id=2 in seed_default_materials)
      doc->recompute_scene();
    }
    add_document_tab(doc);
  }
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
  if (auto* vp = current_viewport()) {
    vp->set_tool(mode);
    sync_create_tool_actions(vp->tool_mode());
    return;
  }
  sync_create_tool_actions(ToolMode::None);
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
}

void MainWindow::show_documents() {
  if (tabs_->count() == 0) {
    show_home();
    return;
  }
  stack_->setCurrentWidget(tabs_);
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
        item.path = QString::fromStdString(path.string());
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
    auto* vp = qobject_cast<DocumentViewport*>(tabs_->widget(i));
    if (!vp) {
      continue;
    }
    const auto& doc_path = vp->document().path();
    if (doc_path.empty()) {
      continue;
    }
    if (QFileInfo(QString::fromStdString(doc_path.string())).absoluteFilePath() == abs) {
      return i;
    }
  }
  return -1;
}

Result<void> MainWindow::populate_document_meshes(Document& document, RenderThread& thread) {
  for (auto& [asset_id, asset] : document.meshes()) {
    auto gpu_id = thread.upload_mesh(asset_id, asset.cpu);
    if (!gpu_id) {
      return Err(gpu_id.error());
    }
  }
  document.recompute_scene();
  return {};
}

void MainWindow::open_about() {
  AboutDialog dialog(this);
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
  bool any_default_visible = false;
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
    any_default_visible =
        any_default_visible || (visible && item.in_default_group);
  }
  if (plugin_commands_group_ != nullptr) {
    plugin_commands_group_->setVisible(any_default_visible);
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

void MainWindow::add_document_tab(std::shared_ptr<Document> document,
                                  const ViewportState* viewport) {
  const RenderDeviceConfig config = AppSettings::instance().render_device_config();
  auto thread = RenderThreadPool::instance().acquire(config);
  if (!thread) {
    QMessageBox::critical(
        this, tr("Render"),
        tr("Failed to create %1 render thread.")
            .arg(QString::fromUtf8(to_string(config.backend))));
    return;
  }
  if (auto r = populate_document_meshes(*document, *thread); !r) {
    QMessageBox::critical(this, tr("Upload"), QString::fromStdString(r.error()));
    return;
  }
  // Parent after addTab so the first present sees a real laid-out size. Applying
  // viewport (and redrawing) before addTab can create a tiny swapchain that only
  // fills the top-left corner of the window.
  auto* vp = new DocumentViewport(document, thread, nullptr);
  connect(vp, &DocumentViewport::tool_mode_changed, this, [this](ToolMode mode) {
    sync_create_tool_actions(mode);
  });
  connect(vp, &DocumentViewport::status_message, this, [this](const QString& text) {
    statusBar()->showMessage(text, 5000);
  });
  connect(vp, &DocumentViewport::selection_changed, this, &MainWindow::refresh_property_panel);
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::refresh_property_panel);
  connect(vp, &DocumentViewport::selection_changed, this, &MainWindow::refresh_handle_inspector);
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::refresh_handle_inspector);
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
  bind_plugin_session();
}

void MainWindow::new_document() {
  auto document = std::make_shared<Document>(tr("Untitled").toStdString());
  document->add_import_mesh("cube", make_demo_cube(), Mat4::identity(), {0.75f, 0.78f, 0.82f});
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

  const auto file = std::filesystem::path(info.absoluteFilePath().toStdString());

  if (is_tdoc_document_path(file)) {
    auto loaded = load_document(file);
    if (!loaded) {
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(loaded.error()));
      return false;
    }
    ViewportState vp_storage = loaded->viewport;
    const bool has_viewport = loaded->has_viewport;
    auto document = std::make_shared<Document>(std::move(loaded->document));
    // Prefer the on-disk filename so tabs/recent show .tdoc even if an older
    // file still has an imported .obj name in META.
    document->set_path(file);
    document->set_name(file.filename().string());
    add_document_tab(document, has_viewport ? &vp_storage : nullptr);

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
    statusBar()->showMessage(tr("Loaded %1").arg(info.absoluteFilePath()), 5000);
    return true;
  }

  if (info.suffix().compare(QStringLiteral("ifc"), Qt::CaseInsensitive) == 0) {
    auto tree = format_ifc_spatial_tree(file);
    if (!tree) {
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(tree.error()));
      return false;
    }
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

  Result<MeshCpu> mesh = Err("no loader");
  if (occt_supports_extension(file)) {
    auto* ops = ShapeOpsRegistry::instance().find("occt");
    if (!ops) {
      QMessageBox::critical(this, tr("Open"), tr("OCCT ShapeOps is not registered."));
      return false;
    }
    auto shape = ops->read_file(file);
    if (!shape) {
      QMessageBox::critical(this, tr("Open"), QString::fromStdString(shape.error()));
      return false;
    }
    mesh = (*shape)->tessellate(0.1);
  } else {
    mesh = load_mesh_file(file);
  }
  if (!mesh) {
    QMessageBox::critical(this, tr("Open"), QString::fromStdString(mesh.error()));
    return false;
  }
  auto document = std::make_shared<Document>(file.filename().string());
  document->set_path(file);
  const bool has_colors = mesh_has_vertex_colors(*mesh);
  const Vec3 color = has_colors ? Vec3{1.f, 1.f, 1.f} : Vec3{0.75f, 0.78f, 0.82f};
  const std::uint64_t mesh_id =
      document->add_import_mesh(file.filename().string(), std::move(*mesh), Mat4::identity(),
                                color);
  add_document_tab(document);

  const MeshAsset* asset = document->mesh(mesh_id);
  const QImage thumb = render_mesh_thumbnail(asset->cpu);
  const QString thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
  recent_.add(info.absoluteFilePath(), thumb_path);
  refresh_home();
  statusBar()->showMessage(tr("Loaded %1").arg(info.absoluteFilePath()), 5000);
  return true;
}

void MainWindow::open_file() {
  const QString filters =
      tr("All Supported (*.tdoc *.gltf *.glb *.obj *.step *.stp *.iges *.igs *.brep *.ifc);;"
         "Tamias (*.tdoc);;"
         "Meshes (*.gltf *.glb *.obj);;"
         "CAD (*.step *.stp *.iges *.igs *.brep);;"
         "IFC (*.ifc);;"
         "glTF (*.gltf *.glb);;OBJ (*.obj);;"
         "STEP (*.step *.stp);;IGES (*.iges *.igs);;BREP (*.brep)");
  const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), filters);
  if (path.isEmpty()) {
    return;
  }
  open_path(path);
}

bool MainWindow::is_obj_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0;
}

bool MainWindow::is_tdoc_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("tdoc"), Qt::CaseInsensitive) == 0;
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
  const auto file = std::filesystem::path(abs_path.toStdString());
  if (auto r = save_mesh_file(file, *mesh); !r) {
    QMessageBox::critical(this, tr("Save"), QString::fromStdString(r.error()));
    return false;
  }

  document.set_path(file);
  document.set_name(file.filename().string());
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
  const auto file = std::filesystem::path(abs_path.toStdString());
  // Update identity before serialize so META stores the .tdoc name, not the
  // imported .obj/.step source name.
  document.set_path(file);
  document.set_name(file.filename().string());
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
  statusBar()->showMessage(tr("Saved successfully: %1").arg(path), 8000);
  QMessageBox::information(this, tr("Save"), tr("Saved successfully:\n%1").arg(path));
}

bool MainWindow::save_file() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }

  // Save always writes the whole scene as .tdoc. Imported .obj/.step paths are
  // not overwritten — prompt Save As so the project file is created explicitly.
  const auto& doc_path = vp->document().path();
  if (!doc_path.empty() && is_tdoc_path(QString::fromStdString(doc_path.string()))) {
    return write_tdoc_document(QString::fromStdString(doc_path.string()));
  }
  return save_file_as();
}

bool MainWindow::save_file_as() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }

  QString suggested;
  const auto& doc_path = vp->document().path();
  if (!doc_path.empty()) {
    QFileInfo info(QString::fromStdString(doc_path.string()));
    suggested = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                QStringLiteral(".tdoc");
  } else {
    suggested = QString::fromStdString(vp->document().name());
    if (!is_tdoc_path(suggested) && !is_obj_path(suggested)) {
      suggested += QStringLiteral(".tdoc");
    } else if (is_obj_path(suggested)) {
      suggested = QFileInfo(suggested).completeBaseName() + QStringLiteral(".tdoc");
    }
  }

  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save As"), suggested,
      tr("Tamias Document (*.tdoc);;OBJ Mesh Export (*.obj)"));
  if (path.isEmpty()) {
    return false;
  }
  if (is_obj_path(path) ||
      (!is_tdoc_path(path) && path.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive))) {
    return write_selected_mesh(path);
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
    property_panel_->show_entity(nullptr, nullptr, tr("Imported mesh: %1\n(no editable parameters)")
                                                        .arg(QString::fromStdString(node->name)));
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

void MainWindow::frame_all() {
  if (auto* vp = current_viewport()) {
    vp->frame_scene();
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
  if (auto* vp = current_viewport()) {
    mode = vp->render_mode();
  }
  const QSignalBlocker b0(wireframe_action_);
  const QSignalBlocker b1(shaded_action_);
  const QSignalBlocker b2(realistic_action_);
  wireframe_action_->setChecked(mode == RenderMode::Wireframe);
  shaded_action_->setChecked(mode == RenderMode::Shaded);
  realistic_action_->setChecked(mode == RenderMode::Realistic);
}

void MainWindow::bind_plugin_session() {
  auto* vp = current_viewport();
  if (vp == nullptr) {
    plugin_host_.unbind();
    plugin_host_.set_point_input_handlers({}, {});
    return;
  }
  plugin_host_.bind(&vp->document(), &vp->command_system(), [vp] { vp->refresh_after_edit(); });
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
  QMainWindow::closeEvent(event);
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
