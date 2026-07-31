#include "main_window.h"

#include "app_settings.h"
#include "core/log.h"
#include "io/mesh_io.h"
#include "mesh_thumbnail.h"
#include "settings_dialog.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>

namespace tamias {

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
  connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::on_tab_changed);
  connect(home_, &HomePage::newDemoRequested, this, &MainWindow::new_demo_document);
  connect(home_, &HomePage::openRequested, this, &MainWindow::open_file);
  connect(home_, &HomePage::fileActivated, this, &MainWindow::open_recent_path);
  connect(home_, &HomePage::missingFileActivated, this, &MainWindow::on_missing_recent);
  connect(home_, &HomePage::openDocumentActivated, this, &MainWindow::activate_open_document);
  connect(home_, &HomePage::settingsRequested, this, &MainWindow::open_settings);

  // File is a navigation control to the welcome page — no dropdown actions.
  menuBar()->addAction(tr("&File"), this, &MainWindow::show_home);

  auto* open_action = new QAction(tr("Open..."), this);
  open_action->setShortcut(QKeySequence::Open);
  connect(open_action, &QAction::triggered, this, &MainWindow::open_file);
  addAction(open_action);

  auto* settings_action = new QAction(tr("&Settings..."), this);
  settings_action->setShortcut(QKeySequence(tr("Ctrl+,")));
  connect(settings_action, &QAction::triggered, this, &MainWindow::open_settings);
  addAction(settings_action);

  render_mode_group_ = new QActionGroup(this);
  render_mode_group_->setExclusive(true);

  wireframe_action_ = new QAction(tr("&Wireframe"), this);
  wireframe_action_->setCheckable(true);
  wireframe_action_->setShortcut(QKeySequence(tr("Ctrl+1")));
  wireframe_action_->setStatusTip(tr("Line drawing — edges only"));
  wireframe_action_->setData(static_cast<int>(RenderMode::Wireframe));

  shaded_action_ = new QAction(tr("&Shaded"), this);
  shaded_action_->setCheckable(true);
  shaded_action_->setChecked(true);
  shaded_action_->setShortcut(QKeySequence(tr("Ctrl+2")));
  shaded_action_->setStatusTip(tr("Simple shaded solid display"));
  shaded_action_->setData(static_cast<int>(RenderMode::Shaded));

  realistic_action_ = new QAction(tr("&Realistic"), this);
  realistic_action_->setCheckable(true);
  realistic_action_->setShortcut(QKeySequence(tr("Ctrl+3")));
  realistic_action_->setStatusTip(tr("Lit display with specular highlights"));
  realistic_action_->setData(static_cast<int>(RenderMode::Realistic));

  render_mode_group_->addAction(wireframe_action_);
  render_mode_group_->addAction(shaded_action_);
  render_mode_group_->addAction(realistic_action_);
  connect(render_mode_group_, &QActionGroup::triggered, this, [this](QAction* action) {
    set_render_mode(static_cast<RenderMode>(action->data().toInt()));
  });

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  auto* display_menu = view_menu->addMenu(tr("&Display Mode"));
  display_menu->addAction(wireframe_action_);
  display_menu->addAction(shaded_action_);
  display_menu->addAction(realistic_action_);
  view_menu->addSeparator();
  view_menu->addAction(tr("&Frame All"), this, &MainWindow::frame_all, QKeySequence(tr("F")));

  // Settings live under Tools (VS / CAD), and also on the File welcome page.
  auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(settings_action);
  tools_menu->addSeparator();
  auto* exit_action = new QAction(tr("E&xit"), this);
  exit_action->setShortcut(QKeySequence::Quit);
  connect(exit_action, &QAction::triggered, this, &QWidget::close);
  tools_menu->addAction(exit_action);

  auto* toolbar = addToolBar(tr("Display"));
  toolbar->setObjectName(QStringLiteral("displayToolbar"));
  toolbar->setMovable(false);
  toolbar->addAction(wireframe_action_);
  toolbar->addAction(shaded_action_);
  toolbar->addAction(realistic_action_);
  toolbar->addSeparator();
  toolbar->addAction(tr("Frame All"), this, &MainWindow::frame_all);

  statusBar()->showMessage(tr("Ready — Open glTF/OBJ or create a demo cube"));
  show_home();
}

void MainWindow::show_home() {
  refresh_home();
  stack_->setCurrentWidget(home_);
  sync_render_mode_actions();
}

void MainWindow::show_documents() {
  if (tabs_->count() == 0) {
    show_home();
    return;
  }
  stack_->setCurrentWidget(tabs_);
  sync_render_mode_actions();
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
  for (auto& [id, asset] : document.meshes()) {
    (void)id;
    auto gpu_id = thread.upload_mesh(asset.cpu);
    if (!gpu_id) {
      return Err(gpu_id.error());
    }
    asset.gpu_mesh_id = *gpu_id;
  }
  for (auto& node : document.scene().nodes()) {
    if (auto* asset = document.mesh(node.mesh_asset_id)) {
      node.gpu_mesh_id = asset->gpu_mesh_id;
      node.world_bounds = asset->cpu.bounds;
    }
  }
  return {};
}

void MainWindow::open_settings() {
  SettingsDialog dialog(this);
  dialog.exec();
}

void MainWindow::add_document_tab(std::shared_ptr<Document> document) {
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
  auto* viewport = new DocumentViewport(document, tabs_);
  const int index = tabs_->addTab(viewport, QString::fromStdString(document->name()));
  tabs_->setCurrentIndex(index);
  show_documents();
  sync_render_mode_actions();
}

void MainWindow::new_demo_document() {
  auto document = std::make_shared<Document>("Demo Cube");
  MeshAsset asset{};
  asset.name = "cube";
  asset.cpu = make_demo_cube();
  auto& stored = document->add_mesh(std::move(asset));
  SceneNode node{};
  node.name = "cube";
  node.mesh_asset_id = stored.id;
  node.world_bounds = stored.cpu.bounds;
  document->scene().add_node(std::move(node));
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
  auto mesh = load_mesh_file(file);
  if (!mesh) {
    QMessageBox::critical(this, tr("Open"), QString::fromStdString(mesh.error()));
    return false;
  }
  auto document = std::make_shared<Document>(file.filename().string());
  document->set_path(file);
  MeshAsset asset{};
  asset.name = file.filename().string();
  asset.cpu = std::move(*mesh);
  auto& stored = document->add_mesh(std::move(asset));
  SceneNode node{};
  node.name = stored.name;
  node.mesh_asset_id = stored.id;
  node.world_bounds = stored.cpu.bounds;
  document->scene().add_node(std::move(node));
  add_document_tab(document);

  const QImage thumb = render_mesh_thumbnail(stored.cpu);
  const QString thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
  recent_.add(info.absoluteFilePath(), thumb_path);
  refresh_home();
  statusBar()->showMessage(tr("Loaded %1").arg(info.absoluteFilePath()), 5000);
  return true;
}

void MainWindow::open_file() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Mesh"), QString(),
      tr("Meshes (*.gltf *.glb *.obj);;glTF (*.gltf *.glb);;OBJ (*.obj)"));
  if (path.isEmpty()) {
    return;
  }
  open_path(path);
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

void MainWindow::set_render_mode(RenderMode mode) {
  if (auto* vp = current_viewport()) {
    vp->set_render_mode(mode);
  }
  sync_render_mode_actions();
}

void MainWindow::sync_render_mode_actions() {
  const auto* vp = current_viewport();
  const RenderMode mode = vp ? vp->render_mode() : RenderMode::Shaded;
  const bool enabled = vp != nullptr;
  wireframe_action_->setEnabled(enabled);
  shaded_action_->setEnabled(enabled);
  realistic_action_->setEnabled(enabled);
  switch (mode) {
    case RenderMode::Wireframe:
      wireframe_action_->setChecked(true);
      break;
    case RenderMode::Realistic:
      realistic_action_->setChecked(true);
      break;
    case RenderMode::Shaded:
    default:
      shaded_action_->setChecked(true);
      break;
  }
}

void MainWindow::on_tab_changed(int) { sync_render_mode_actions(); }

void MainWindow::frame_all() {
  if (auto* vp = current_viewport()) {
    vp->frame_scene();
  }
}

void MainWindow::close_tab(int index) {
  if (auto* w = tabs_->widget(index)) {
    tabs_->removeTab(index);
    delete w;
  }
  if (tabs_->count() == 0) {
    show_home();
  } else {
    sync_render_mode_actions();
  }
}

}  // namespace tamias
