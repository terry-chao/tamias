#include "main_window.h"

#include "app_settings.h"
#include "core/log.h"
#include "io/mesh_io.h"
#include "mesh_thumbnail.h"
#include "modeling/occt_shape_ops.h"
#include "modeling/shape_ops.h"
#include "settings_dialog.h"

#include <QAction>
#include <QCoreApplication>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QImage>
#include <QKeySequence>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QVector>

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
  connect(home_, &HomePage::newDemoRequested, this, &MainWindow::new_demo_document);
  connect(home_, &HomePage::openRequested, this, &MainWindow::open_file);
  connect(home_, &HomePage::fileActivated, this, &MainWindow::open_recent_path);
  connect(home_, &HomePage::missingFileActivated, this, &MainWindow::on_missing_recent);
  connect(home_, &HomePage::recentRemoveRequested, this, [this](const QString& path) {
    recent_.remove(path);
    refresh_home();
  });
  connect(home_, &HomePage::openDocumentActivated, this, &MainWindow::activate_open_document);
  connect(home_, &HomePage::settingsRequested, this, &MainWindow::open_settings);

  // File is a navigation control to the welcome page — no dropdown actions.
  menuBar()->addAction(tr("&File"), this, &MainWindow::show_home);

  auto* open_action = new QAction(style()->standardIcon(QStyle::SP_DialogOpenButton),
                                  tr("Open File"), this);
  open_action->setShortcut(QKeySequence::Open);
  open_action->setToolTip(tr("Open a model file"));
  connect(open_action, &QAction::triggered, this, &MainWindow::open_file);
  addAction(open_action);

  auto* save_action = new QAction(style()->standardIcon(QStyle::SP_DialogSaveButton),
                                  tr("Save"), this);
  save_action->setShortcut(QKeySequence::Save);
  save_action->setToolTip(tr("Save the selected model"));
  connect(save_action, &QAction::triggered, this, &MainWindow::save_file);
  addAction(save_action);

  auto* save_as_action = new QAction(
      themed_mask_icon(QStringLiteral(":/icons/save_as.svg"),
                       palette().color(QPalette::WindowText)),
      tr("Save As…"), this);
  save_as_action->setShortcut(QKeySequence::SaveAs);
  save_as_action->setToolTip(tr("Save the selected model to a new file"));
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

  auto* settings_action =
      new QAction(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr("Settings"), this);
  settings_action->setShortcut(QKeySequence(tr("Ctrl+,")));
  settings_action->setToolTip(tr("Open settings"));
  connect(settings_action, &QAction::triggered, this, &MainWindow::open_settings);
  addAction(settings_action);

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  view_menu->addAction(frame_all_action);

  // Settings live under Tools (VS / CAD), and also on the File welcome page.
  auto* tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(settings_action);
  tools_menu->addSeparator();
  auto* exit_action = new QAction(tr("E&xit"), this);
  exit_action->setShortcut(QKeySequence::Quit);
  connect(exit_action, &QAction::triggered, this, &QWidget::close);
  tools_menu->addAction(exit_action);

  auto* toolbar = addToolBar(tr("Main"));
  toolbar->setObjectName(QStringLiteral("mainToolbar"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->addAction(open_action);
  toolbar->addAction(save_action);
  toolbar->addAction(save_as_action);
  toolbar->addAction(frame_all_action);
  toolbar->addAction(settings_action);

  statusBar()->showMessage(tr("Ready — Open a model or try the demo"));
  show_home();
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
  auto* viewport = new DocumentViewport(document, thread, tabs_);
  const int index = tabs_->addTab(viewport, QString::fromStdString(document->name()));
  tabs_->setCurrentIndex(index);
  show_documents();
}

void MainWindow::new_demo_document() {
  const QString relative = QStringLiteral("assets/samples/alvin.obj");
  QString path = QDir(QCoreApplication::applicationDirPath()).filePath(relative);
  if (!QFileInfo::exists(path)) {
    path = QDir(QStringLiteral(TAMIAS_SOURCE_DIR)).filePath(relative);
  }
  if (!QFileInfo::exists(path)) {
    QMessageBox::warning(this, tr("Demo"),
                         tr("Demo model not found:\n%1").arg(relative));
    return;
  }
  open_path(path);
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
  Result<MeshCpu> mesh = Err("no loader");
  if (occt_supports_extension(file)) {
#if defined(TAMIAS_HAS_OCCT)
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
#else
    QMessageBox::critical(
        this, tr("Open"),
        tr("This build was compiled without OCCT. Set OCCT_ROOT and rebuild."));
    return false;
#endif
  } else {
    mesh = load_mesh_file(file);
  }
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
  if (mesh_has_vertex_colors(stored.cpu)) {
    node.color = {1.f, 1.f, 1.f};
  }
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
  QString filters = tr("Meshes (*.gltf *.glb *.obj);;glTF (*.gltf *.glb);;OBJ (*.obj)");
#if defined(TAMIAS_HAS_OCCT)
  filters = tr("All Supported (*.gltf *.glb *.obj *.step *.stp *.iges *.igs *.brep);;"
               "Meshes (*.gltf *.glb *.obj);;"
               "CAD (*.step *.stp *.iges *.igs *.brep);;"
               "glTF (*.gltf *.glb);;OBJ (*.obj);;"
               "STEP (*.step *.stp);;IGES (*.iges *.igs);;BREP (*.brep)");
#endif
  const QString path = QFileDialog::getOpenFileName(this, tr("Open"), QString(), filters);
  if (path.isEmpty()) {
    return;
  }
  open_path(path);
}

bool MainWindow::is_obj_path(const QString& path) {
  return QFileInfo(path).suffix().compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0;
}

const MeshCpu* MainWindow::selected_mesh(Document& document) const {
  const SceneNode* node = document.scene().selected_node();
  if (!node) {
    return nullptr;
  }
  const MeshAsset* asset = document.mesh(node->mesh_asset_id);
  return asset ? &asset->cpu : nullptr;
}

bool MainWindow::write_selected_mesh(const QString& path) {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return false;
  }
  Document& document = vp->document();
  const MeshCpu* mesh = selected_mesh(document);
  if (!mesh) {
    QMessageBox::information(this, tr("Save"),
                             tr("Select a model in the viewport, then save again."));
    return false;
  }

  QString out_path = path;
  if (!is_obj_path(out_path)) {
    out_path += QStringLiteral(".obj");
  }
  const auto file = std::filesystem::path(QFileInfo(out_path).absoluteFilePath().toStdString());
  if (auto r = save_mesh_file(file, *mesh); !r) {
    QMessageBox::critical(this, tr("Save"), QString::fromStdString(r.error()));
    return false;
  }

  document.set_path(file);
  document.set_name(file.filename().string());
  if (const int index = tabs_->indexOf(vp); index >= 0) {
    tabs_->setTabText(index, QString::fromStdString(document.name()));
  }

  const QFileInfo info(QString::fromStdString(file.string()));
  const QImage thumb = render_mesh_thumbnail(*mesh);
  const QString thumb_path = save_mesh_thumbnail(info.absoluteFilePath(), thumb);
  recent_.add(info.absoluteFilePath(), thumb_path);
  refresh_home();
  statusBar()->showMessage(tr("Saved %1").arg(info.absoluteFilePath()), 5000);
  return true;
}

void MainWindow::save_file() {
  auto* vp = current_viewport();
  if (!vp) {
    QMessageBox::information(this, tr("Save"), tr("Open a document first."));
    return;
  }
  if (!selected_mesh(vp->document())) {
    QMessageBox::information(this, tr("Save"),
                             tr("Select a model in the viewport, then save again."));
    return;
  }

  const auto& doc_path = vp->document().path();
  if (!doc_path.empty() && is_obj_path(QString::fromStdString(doc_path.string()))) {
    write_selected_mesh(QString::fromStdString(doc_path.string()));
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
  if (!selected_mesh(vp->document())) {
    QMessageBox::information(this, tr("Save"),
                             tr("Select a model in the viewport, then save again."));
    return;
  }

  QString suggested;
  const auto& doc_path = vp->document().path();
  if (!doc_path.empty()) {
    QFileInfo info(QString::fromStdString(doc_path.string()));
    suggested = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() +
                QStringLiteral(".obj");
  } else {
    suggested = QString::fromStdString(vp->document().name());
    if (!is_obj_path(suggested)) {
      suggested += QStringLiteral(".obj");
    }
  }

  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save As"), suggested, tr("OBJ (*.obj)"));
  if (path.isEmpty()) {
    return;
  }
  write_selected_mesh(path);
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
  }
}

}  // namespace tamias
