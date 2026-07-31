#include "main_window.h"

#include "core/log.h"
#include "io/mesh_io.h"

#include <QFileDialog>
#include <QIcon>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>

namespace tamias {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  setWindowTitle("Tamias");
  setWindowIcon(QIcon(QStringLiteral(":/branding/logo.png")));
  resize(1280, 800);

  tabs_ = new QTabWidget(this);
  tabs_->setTabsClosable(true);
  tabs_->setDocumentMode(true);
  setCentralWidget(tabs_);
  connect(tabs_, &QTabWidget::tabCloseRequested, this, &MainWindow::close_tab);

  auto* file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("&New Demo Cube"), this, &MainWindow::new_demo_document);
  file_menu->addAction(tr("&Open..."), this, &MainWindow::open_file, QKeySequence::Open);
  file_menu->addSeparator();
  file_menu->addAction(tr("E&xit"), this, &QWidget::close, QKeySequence::Quit);

  auto* view_menu = menuBar()->addMenu(tr("&View"));
  view_menu->addAction(tr("&Shaded"), this, &MainWindow::set_shaded);
  view_menu->addAction(tr("&Wireframe"), this, &MainWindow::set_wireframe);
  view_menu->addAction(tr("&Frame All"), this, &MainWindow::frame_all, QKeySequence(tr("F")));

  statusBar()->showMessage(tr("Ready — Open glTF/OBJ or create a demo cube"));
  new_demo_document();
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

void MainWindow::add_document_tab(std::shared_ptr<Document> document) {
  RenderDeviceConfig config{};
  auto thread = RenderThreadPool::instance().acquire(config);
  if (!thread) {
    QMessageBox::critical(this, tr("Render"), tr("Failed to create Vulkan render thread."));
    return;
  }
  if (auto r = populate_document_meshes(*document, *thread); !r) {
    QMessageBox::critical(this, tr("Upload"), QString::fromStdString(r.error()));
    return;
  }
  auto* viewport = new DocumentViewport(document, tabs_);
  const int index = tabs_->addTab(viewport, QString::fromStdString(document->name()));
  tabs_->setCurrentIndex(index);
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

void MainWindow::open_file() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Mesh"), QString(),
      tr("Meshes (*.gltf *.glb *.obj);;glTF (*.gltf *.glb);;OBJ (*.obj)"));
  if (path.isEmpty()) {
    return;
  }
  const auto file = std::filesystem::path(path.toStdString());
  auto mesh = load_mesh_file(file);
  if (!mesh) {
    QMessageBox::critical(this, tr("Open"), QString::fromStdString(mesh.error()));
    return;
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
  statusBar()->showMessage(tr("Loaded %1").arg(path), 5000);
}

void MainWindow::set_shaded() {
  if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->currentWidget())) {
    vp->set_render_mode(RenderMode::Shaded);
  }
}

void MainWindow::set_wireframe() {
  if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->currentWidget())) {
    vp->set_render_mode(RenderMode::Wireframe);
  }
}

void MainWindow::frame_all() {
  if (auto* vp = qobject_cast<DocumentViewport*>(tabs_->currentWidget())) {
    vp->frame_scene();
  }
}

void MainWindow::close_tab(int index) {
  if (auto* w = tabs_->widget(index)) {
    tabs_->removeTab(index);
    delete w;
  }
}

}  // namespace tamias
