#include "main_window.h"

#include "app_settings.h"
#include "engine/core/log.h"
#include "engine/document/document_io.h"
#include "entity/box_entity.h"
#include "engine/io/mesh_io.h"
#include "mesh_thumbnail.h"
#include "engine/modeling/occt_shape_ops.h"
#include "engine/modeling/shape_ops.h"
#include "property_panel.h"
#include "settings_dialog.h"

#include <QAction>
#include <QActionGroup>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QSize>
#include <QSizePolicy>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>
#include <QVector>
#include <QWidget>

namespace tamias {
namespace {

QIcon themed_mask_icon(const QString& resource, const QColor& color, int extent = 16) {
  const QIcon source(resource);
  QIcon result;
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
  return result;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Tamias");
  setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));
  resize(1280, 800);

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
    sync_render_mode_actions();
    refresh_property_panel();
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

  auto* new_action = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/new.svg"),
                       palette().color(QPalette::WindowText)),
      tr("New"), this);
  new_action->setShortcut(QKeySequence::New);
  new_action->setToolTip(tr("New document with a cube"));
  connect(new_action, &QAction::triggered, this, &MainWindow::new_document);
  addAction(new_action);

  auto* open_action = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                  tr("Open File"), this);
  open_action->setShortcut(QKeySequence::Open);
  open_action->setToolTip(tr("Open a model file"));
  connect(open_action, &QAction::triggered, this, &MainWindow::open_file);
  addAction(open_action);

  auto* save_action = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                  tr("Save"), this);
  save_action->setShortcut(QKeySequence::Save);
  save_action->setToolTip(tr("Save the document"));
  connect(save_action, &QAction::triggered, this, &MainWindow::save_file);
  addAction(save_action);

  auto* save_as_action = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/save_as.svg"),
                       palette().color(QPalette::WindowText)),
      tr("Save As…"), this);
  save_as_action->setShortcut(QKeySequence::SaveAs);
  save_as_action->setToolTip(tr("Save the document to a new file"));
  connect(save_as_action, &QAction::triggered, this, &MainWindow::save_file_as);
  addAction(save_as_action);

  auto* frame_all_action = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/frame_all.svg"),
                       palette().color(QPalette::WindowText)),
      tr("&Frame All"), this);
  frame_all_action->setShortcut(QKeySequence(tr("F")));
  frame_all_action->setToolTip(tr("Frame all geometry in the view"));
  connect(frame_all_action, &QAction::triggered, this, &MainWindow::frame_all);
  addAction(frame_all_action);

  create_group_ = new QActionGroup(this);
  create_group_->setExclusive(true);

  wall_action_ = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/wall.svg"),
                       palette().color(QPalette::WindowText)),
      tr("Wall"), this);
  wall_action_->setCheckable(true);
  wall_action_->setProperty("toolMode", static_cast<int>(ToolMode::Wall));
  wall_action_->setToolTip(tr("Create a wall: click start, then click end"));
  connect(wall_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Wall); });
  create_group_->addAction(wall_action_);
  addAction(wall_action_);

  box_action_ = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/box.svg"),
                       palette().color(QPalette::WindowText)),
      tr("Box"), this);
  box_action_->setCheckable(true);
  box_action_->setProperty("toolMode", static_cast<int>(ToolMode::Box));
  box_action_->setToolTip(tr("Create a box: click to place"));
  connect(box_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Box); });
  create_group_->addAction(box_action_);
  addAction(box_action_);

  cylinder_action_ = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/cylinder.svg"),
                       palette().color(QPalette::WindowText)),
      tr("Cylinder"), this);
  cylinder_action_->setCheckable(true);
  cylinder_action_->setProperty("toolMode", static_cast<int>(ToolMode::Cylinder));
  cylinder_action_->setToolTip(tr("Create a cylinder: click to place"));
  connect(cylinder_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Cylinder); });
  create_group_->addAction(cylinder_action_);
  addAction(cylinder_action_);

  beam_action_ = new QAction(tr("Beam"), this);
  beam_action_->setCheckable(true);
  beam_action_->setProperty("toolMode", static_cast<int>(ToolMode::Beam));
  beam_action_->setToolTip(tr("Create a beam: click start, then click end"));
  connect(beam_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Beam); });
  create_group_->addAction(beam_action_);
  addAction(beam_action_);

  column_action_ = new QAction(tr("Column"), this);
  column_action_->setCheckable(true);
  column_action_->setProperty("toolMode", static_cast<int>(ToolMode::Column));
  column_action_->setToolTip(tr("Create a column: click to place"));
  connect(column_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Column); });
  create_group_->addAction(column_action_);
  addAction(column_action_);

  slab_action_ = new QAction(tr("Slab"), this);
  slab_action_->setCheckable(true);
  slab_action_->setProperty("toolMode", static_cast<int>(ToolMode::Slab));
  slab_action_->setToolTip(tr("Create a slab: click to place"));
  connect(slab_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Slab); });
  create_group_->addAction(slab_action_);
  addAction(slab_action_);

  door_action_ = new QAction(tr("Door"), this);
  door_action_->setCheckable(true);
  door_action_->setProperty("toolMode", static_cast<int>(ToolMode::Door));
  door_action_->setToolTip(tr("Create a door: click to place"));
  connect(door_action_, &QAction::triggered, this, [this] { set_create_tool(ToolMode::Door); });
  create_group_->addAction(door_action_);
  addAction(door_action_);

  window_action_ = new QAction(tr("Window"), this);
  window_action_->setCheckable(true);
  window_action_->setProperty("toolMode", static_cast<int>(ToolMode::Window));
  window_action_->setToolTip(tr("Create a window: click to place"));
  connect(window_action_, &QAction::triggered, this,
          [this] { set_create_tool(ToolMode::Window); });
  create_group_->addAction(window_action_);
  addAction(window_action_);

  fillet_action_ = new QAction(tr("Fillet"), this);
  fillet_action_->setToolTip(tr("Fillet the selected entity's first edge"));
  connect(fillet_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->fillet_selected(0.05);
    }
  });
  addAction(fillet_action_);

  chamfer_action_ = new QAction(tr("Chamfer"), this);
  chamfer_action_->setToolTip(tr("Chamfer the selected entity's first edge"));
  connect(chamfer_action_, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->chamfer_selected(0.05);
    }
  });
  addAction(chamfer_action_);

  auto* settings_action =
      new QAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Settings"), this);
  settings_action->setShortcut(QKeySequence(tr("Ctrl+,")));
  settings_action->setToolTip(tr("Open settings"));
  connect(settings_action, &QAction::triggered, this, &MainWindow::open_settings);
  addAction(settings_action);

  auto* home_action = new QAction(tr("&Home"), this);
  home_action->setToolTip(tr("Back to the welcome page"));
  connect(home_action, &QAction::triggered, this, &MainWindow::show_home);
  addAction(home_action);

  auto* undo_action =
      new QAction(style()->standardIcon(QStyle::SP_ArrowBack), tr("&Undo"), this);
  undo_action->setShortcut(QKeySequence::Undo);
  connect(undo_action, &QAction::triggered, this, [this] {
    if (auto* vp = current_viewport()) {
      vp->undo();
    }
  });
  addAction(undo_action);

  auto* redo_action =
      new QAction(style()->standardIcon(QStyle::SP_ArrowForward), tr("&Redo"), this);
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

  // ===== 菜单栏 =====
  auto* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(new_action);
  file_menu->addAction(open_action);
  file_menu->addSeparator();
  file_menu->addAction(save_action);
  file_menu->addAction(save_as_action);
  file_menu->addSeparator();
  file_menu->addAction(exit_action);

  auto* edit_menu = menuBar()->addMenu(tr("&Edit"));
  edit_menu->addAction(undo_action);
  edit_menu->addAction(redo_action);

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  view_menu->addAction(home_action);
  view_menu->addAction(frame_all_action);
  view_menu->addSeparator();

  auto* display_menu = view_menu->addMenu(tr("&Display Mode"));
  auto* display_group = new QActionGroup(this);
  display_group->setExclusive(true);

  wireframe_action_ = display_menu->addAction(tr("&Wireframe"));
  wireframe_action_->setCheckable(true);
  wireframe_action_->setShortcut(QKeySequence(tr("Ctrl+1")));
  wireframe_action_->setToolTip(tr("Line drawing — edges only"));
  display_group->addAction(wireframe_action_);
  connect(wireframe_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Wireframe);
  });
  addAction(wireframe_action_);

  shaded_action_ = display_menu->addAction(tr("&Shaded"));
  shaded_action_->setCheckable(true);
  shaded_action_->setChecked(true);
  shaded_action_->setShortcut(QKeySequence(tr("Ctrl+2")));
  shaded_action_->setToolTip(tr("Simple shaded solid display"));
  display_group->addAction(shaded_action_);
  connect(shaded_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Shaded);
  });
  addAction(shaded_action_);

  realistic_action_ = display_menu->addAction(tr("&Realistic"));
  realistic_action_->setCheckable(true);
  realistic_action_->setShortcut(QKeySequence(tr("Ctrl+3")));
  realistic_action_->setToolTip(tr("Lit display with specular highlights"));
  display_group->addAction(realistic_action_);
  connect(realistic_action_, &QAction::triggered, this, [this] {
    set_render_mode(RenderMode::Realistic);
  });
  addAction(realistic_action_);

  render_mode_combo_ = new QComboBox(this);
  render_mode_combo_->addItem(tr("Wireframe"), static_cast<int>(RenderMode::Wireframe));
  render_mode_combo_->addItem(tr("Shaded"), static_cast<int>(RenderMode::Shaded));
  render_mode_combo_->addItem(tr("Realistic"), static_cast<int>(RenderMode::Realistic));
  render_mode_combo_->setToolTip(tr("Render mode"));
  connect(render_mode_combo_, &QComboBox::currentIndexChanged, this, [this](int index) {
    if (index >= 0) {
      set_render_mode(static_cast<RenderMode>(render_mode_combo_->itemData(index).toInt()));
    }
  });

  auto* create_menu = menuBar()->addMenu(tr("&Create"));
  create_menu->addAction(wall_action_);
  create_menu->addAction(box_action_);
  create_menu->addAction(cylinder_action_);
  create_menu->addAction(beam_action_);
  create_menu->addAction(column_action_);
  create_menu->addAction(slab_action_);
  create_menu->addAction(door_action_);
  create_menu->addAction(window_action_);

  auto* modify_menu = menuBar()->addMenu(tr("&Modify"));
  modify_menu->addAction(fillet_action_);
  modify_menu->addAction(chamfer_action_);

  auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(settings_action);

  auto* toolbar = addToolBar(tr("Main"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  toolbar->setMovable(false);
  toolbar->setFloatable(false);
  toolbar->setIconSize(QSize(20, 20));
  toolbar->setToolButtonStyle(Qt::ToolButtonIconOnly);
  toolbar->addAction(new_action);
  toolbar->addAction(open_action);
  toolbar->addAction(save_action);
  toolbar->addAction(save_as_action);
  toolbar->addSeparator();
  toolbar->addAction(undo_action);
  toolbar->addAction(redo_action);
  toolbar->addSeparator();
  auto* create_button = new QToolButton(toolbar);
  create_button->setText(tr("Create"));
  create_button->setToolTip(tr("Create a parametric component"));
  create_button->setAutoRaise(true);
  create_button->setPopupMode(QToolButton::InstantPopup);
  create_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto* create_toolbar_menu = new QMenu(create_button);
  auto* basic_menu = create_toolbar_menu->addMenu(tr("Primitives"));
  basic_menu->addAction(box_action_);
  basic_menu->addAction(cylinder_action_);
  auto* building_menu = create_toolbar_menu->addMenu(tr("Building Components"));
  building_menu->addAction(wall_action_);
  building_menu->addAction(beam_action_);
  building_menu->addAction(column_action_);
  building_menu->addAction(slab_action_);
  building_menu->addAction(door_action_);
  building_menu->addAction(window_action_);
  create_button->setMenu(create_toolbar_menu);
  toolbar->addWidget(create_button);

  auto* modify_button = new QToolButton(toolbar);
  modify_button->setText(tr("Modify"));
  modify_button->setToolTip(tr("Modify the selected entity"));
  modify_button->setAutoRaise(true);
  modify_button->setPopupMode(QToolButton::InstantPopup);
  modify_button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  auto* modify_toolbar_menu = new QMenu(modify_button);
  modify_toolbar_menu->addAction(fillet_action_);
  modify_toolbar_menu->addAction(chamfer_action_);
  modify_button->setMenu(modify_toolbar_menu);
  toolbar->addWidget(modify_button);

  auto* toolbar_spacer = new QWidget(toolbar);
  toolbar_spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
  toolbar->addWidget(toolbar_spacer);
  toolbar->addAction(frame_all_action);
  toolbar->addSeparator();
  toolbar->addWidget(render_mode_combo_);

  // 右侧属性面板：展示/编辑选中实体的参数。
  property_panel_ = new PropertyPanel(this);
  auto* property_dock = new QDockWidget(tr("Properties"), this);
  property_dock->setObjectName(QStringLiteral("propertyDock"));
  property_dock->setWidget(property_panel_);
  property_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  addDockWidget(Qt::RightDockWidgetArea, property_dock);
  // View 菜单里加一个开关，属性面板关闭后还能重新打开。
  view_menu->addAction(property_dock->toggleViewAction());
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
  refresh_property_panel();

  statusBar()->showMessage(tr("Ready — Open a model or try the demo"));
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

void MainWindow::set_create_tool(ToolMode mode) {
  if (auto* vp = current_viewport()) {
    vp->set_tool(mode);
    sync_create_tool_actions(mode);
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
  connect(vp, &DocumentViewport::selection_changed, this, &MainWindow::refresh_property_panel);
  connect(vp, &DocumentViewport::document_changed, this, &MainWindow::refresh_property_panel);
  const int index = tabs_->addTab(vp, QString::fromStdString(document->name()));
  tabs_->setCurrentIndex(index);
  show_documents();
  if (viewport) {
    vp->apply_viewport_state(*viewport);
    vp->request_redraw();
  }
  sync_render_mode_actions();
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
      tr("All Supported (*.tdoc *.gltf *.glb *.obj *.step *.stp *.iges *.igs *.brep);;"
         "Tamias (*.tdoc);;"
         "Meshes (*.gltf *.glb *.obj);;"
         "CAD (*.step *.stp *.iges *.igs *.brep);;"
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

void MainWindow::save_file() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return;
  }

  // Save always writes the whole scene as .tdoc. Imported .obj/.step paths are
  // not overwritten — prompt Save As so the project file is created explicitly.
  const auto& doc_path = vp->document().path();
  if (!doc_path.empty() && is_tdoc_path(QString::fromStdString(doc_path.string()))) {
    write_tdoc_document(QString::fromStdString(doc_path.string()));
    return;
  }
  save_file_as();
}

void MainWindow::save_file_as() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return;
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
    return;
  }
  if (is_obj_path(path) ||
      (!is_tdoc_path(path) && path.endsWith(QStringLiteral(".obj"), Qt::CaseInsensitive))) {
    write_selected_mesh(path);
    return;
  }
  write_tdoc_document(path);
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
  if (render_mode_combo_) {
    const QSignalBlocker bc(render_mode_combo_);
    const int idx = render_mode_combo_->findData(static_cast<int>(mode));
    if (idx >= 0) {
      render_mode_combo_->setCurrentIndex(idx);
    }
  }
}

void MainWindow::close_tab(int index) {
  if (auto* w = tabs_->widget(index)) {
    tabs_->removeTab(index);
    delete w;
  }
  if (tabs_->count() == 0) {
    show_home();
  }
}

}  // namespace tamias
