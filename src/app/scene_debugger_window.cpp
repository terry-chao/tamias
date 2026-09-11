#include "scene_debugger_window.h"

#include "document_viewport.h"
#include "engine/core/fs_utf8.h"
#include "engine/render/render_scene_golden.h"
#include "engine/render/scene_debug_log.h"
#include "golden_test_runner.h"
#include "pin_result_dialog.h"
#include "qt_path.h"
#include "render_scene_inspector.h"
#include "replay_viewport.h"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCoreApplication>
#include <QColor>
#include <QDesktopServices>
#include <QDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QInputDialog>
#include <QPainter>
#include <QPixmap>
#include <QRect>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QSlider>
#include <QSplitter>
#include <QSize>
#include <QStatusBar>
#include <QTableWidget>
#include <QToolBar>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace tamias {
namespace {

QString tr_dbg(const char* source) {
  return QCoreApplication::translate("tamias::SceneDebuggerWindow", source);
}

QIcon toolbar_icon(const QString& resource) {
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
        painter.fillRect(QRect(0, 0, extent, extent), QColor(47, 125, 222));
      }
      result.addPixmap(canvas);
    }
  }
  return result;
}

void reveal_path(const std::filesystem::path& path) {
  QDesktopServices::openUrl(QUrl::fromLocalFile(path_to_qstring(path)));
}

QString skip_reason_label(SceneDebugSkipReason reason) {
  switch (reason) {
    case SceneDebugSkipReason::Drawn:
      return tr_dbg("drawn");
    case SceneDebugSkipReason::Hidden:
      return tr_dbg("hidden");
    case SceneDebugSkipReason::Isolated:
      return tr_dbg("isolated");
    case SceneDebugSkipReason::Stepped:
      return tr_dbg("stepped");
    case SceneDebugSkipReason::Culled:
      return tr_dbg("culled");
  }
  return tr_dbg("unknown");
}

QString pipeline_label(const std::string& pipeline) {
  if (pipeline == "transparent") {
    return tr_dbg("transparent");
  }
  if (pipeline == "lines") {
    return tr_dbg("lines");
  }
  if (pipeline == "wire") {
    return tr_dbg("wire");
  }
  if (pipeline == "shaded") {
    return tr_dbg("shaded");
  }
  return QString::fromStdString(pipeline);
}

QString compare_scenes(const RenderScene& a, const RenderScene& b, const QString& a_name,
                       const QString& b_name) {
  auto scene_line = [](const RenderScene& scene, const QString& name) {
    return tr_dbg("%1  digest=%2  items=%3  meshes=%4  hidden=%5\n")
        .arg(name)
        .arg(QString::fromStdString(render_scene_digest(scene)))
        .arg(scene.items.size())
        .arg(scene.meshes.size())
        .arg(scene.hidden_node_ids.size());
  };
  QString out = scene_line(a, a_name) + scene_line(b, b_name);
  const bool same = render_scene_digest(a) == render_scene_digest(b);
  out += same ? tr_dbg("digest: match\n") : tr_dbg("digest: DIFFER\n");
  if (a.items.size() != b.items.size()) {
    out += tr_dbg("item count %1 vs %2\n").arg(a.items.size()).arg(b.items.size());
  }
  const std::size_t n = (std::min)(a.items.size(), b.items.size());
  for (std::size_t i = 0; i < n; ++i) {
    const SceneDrawItem& lhs = a.items[i];
    const SceneDrawItem& rhs = b.items[i];
    if (lhs.node_id == rhs.node_id && lhs.mesh_asset_id == rhs.mesh_asset_id &&
        lhs.albedo_texture_id == rhs.albedo_texture_id && lhs.color.x == rhs.color.x &&
        lhs.color.y == rhs.color.y && lhs.color.z == rhs.color.z) {
      continue;
    }
    out += tr_dbg("item[%1] node %2/%3 mesh %4/%5\n")
               .arg(i)
               .arg(lhs.node_id)
               .arg(rhs.node_id)
               .arg(lhs.mesh_asset_id)
               .arg(rhs.mesh_asset_id);
  }
  return out;
}

}  // namespace

SceneDebuggerWindow::SceneDebuggerWindow(std::shared_ptr<RenderThread> render_thread,
                                         QWidget* parent)
    : QMainWindow(parent) {
  setWindowTitle(tr("Scene Debugger"));

  auto* toolbar = addToolBar(tr("Scene"));
  toolbar->setMovable(false);
  toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
  toolbar->setIconSize(QSize(20, 20));
  auto* open_act = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/open.svg")), tr("Open…"));
  connect(open_act, &QAction::triggered, this, &SceneDebuggerWindow::open_file);
  auto* save_act = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/save.svg")), tr("Save…"));
  connect(save_act, &QAction::triggered, this, &SceneDebuggerWindow::save_snapshot);
  recapture_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/frame_all.svg")), tr("Recapture"));
  recapture_action_->setEnabled(false);
  connect(recapture_action_, &QAction::triggered, this, &SceneDebuggerWindow::recapture);
  auto* pin_act = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/save_as.svg")), tr("Pin for tests…"));
  connect(pin_act, &QAction::triggered, this, &SceneDebuggerWindow::pin_golden);
  auto* dump_act = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/inspector.svg")), tr("Write debug files…"));
  connect(dump_act, &QAction::triggered, this, &SceneDebuggerWindow::write_debug_files);
  auto* compare_act = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/view_2d.svg")), tr("Compare…"));
  connect(compare_act, &QAction::triggered, this, &SceneDebuggerWindow::compare_with_file);
  toolbar->addSeparator();

  auto* mode_group = new QActionGroup(this);
  mode_group->setExclusive(true);
  wireframe_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/wireframe.svg")), tr("Wire"));
  shaded_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/shaded.svg")), tr("Shaded"));
  realistic_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/realistic.svg")), tr("Realistic"));
  wireframe_action_->setCheckable(true);
  shaded_action_->setCheckable(true);
  realistic_action_->setCheckable(true);
  mode_group->addAction(wireframe_action_);
  mode_group->addAction(shaded_action_);
  mode_group->addAction(realistic_action_);
  connect(wireframe_action_, &QAction::triggered, this,
          [this] { set_mode(RenderMode::Wireframe); });
  connect(shaded_action_, &QAction::triggered, this, [this] { set_mode(RenderMode::Shaded); });
  connect(realistic_action_, &QAction::triggered, this,
          [this] { set_mode(RenderMode::Realistic); });
  toolbar->addSeparator();
  axes_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/view_3d.svg")), tr("Axes"));
  axes_action_->setCheckable(true);
  connect(axes_action_, &QAction::toggled, this, [this](bool on) {
    replay_->player().set_show_axes(on);
    replay_->sync_from_player();
  });
  apply_hidden_action_ = toolbar->addAction(toolbar_icon(QStringLiteral(":/icons/visibility.svg")), tr("Captured hidden"));
  apply_hidden_action_->setCheckable(true);
  apply_hidden_action_->setChecked(true);
  apply_hidden_action_->setToolTip(
      tr("Apply the hidden node set stored in the snapshot (floors / isolate)"));
  connect(apply_hidden_action_, &QAction::toggled, this, [this](bool on) {
    replay_->player().set_apply_captured_hidden(on);
    replay_->sync_from_player();
    rebuild_commands();
  });

  replay_ = new ReplayViewport(std::move(render_thread), this);
  inspector_ = new RenderSceneInspector(this);
  inspector_->set_actions_visible(false);
  inspector_->setMinimumWidth(200);

  auto* commands_page = new QWidget(this);
  auto* commands_layout = new QVBoxLayout(commands_page);
  commands_layout->setContentsMargins(8, 8, 8, 8);
  commands_summary_ = new QLabel(commands_page);
  commands_summary_->setWordWrap(true);
  commands_layout->addWidget(commands_summary_);
  commands_ = new QTableWidget(commands_page);
  commands_->setColumnCount(6);
  commands_->setHorizontalHeaderLabels(
      {tr("#"), tr("Node / draw"), tr("Mesh / idx"), tr("Tris / inst"), tr("Reason / pipe"),
       tr("Pass")});
  commands_->horizontalHeader()->setStretchLastSection(true);
  commands_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  commands_->verticalHeader()->setVisible(false);
  commands_->setSelectionBehavior(QAbstractItemView::SelectRows);
  commands_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  commands_->setAlternatingRowColors(true);
  commands_layout->addWidget(commands_);

  auto* replay_wrap = new QWidget(this);
  auto* replay_layout = new QVBoxLayout(replay_wrap);
  replay_layout->setContentsMargins(0, 0, 0, 0);
  replay_layout->setSpacing(4);
  replay_layout->addWidget(replay_, 1);
  auto* step_row = new QHBoxLayout();
  step_label_ = new QLabel(tr("Draws: all"), replay_wrap);
  step_slider_ = new QSlider(Qt::Horizontal, replay_wrap);
  step_slider_->setMinimum(0);
  step_slider_->setMaximum(0);
  step_slider_->setValue(0);
  connect(step_slider_, &QSlider::valueChanged, this, &SceneDebuggerWindow::on_step_changed);
  step_row->addWidget(step_label_);
  step_row->addWidget(step_slider_, 1);
  replay_layout->addLayout(step_row);

  auto* split = new QSplitter(Qt::Horizontal, this);
  split->addWidget(inspector_);
  split->addWidget(replay_wrap);
  split->addWidget(commands_page);
  split->setStretchFactor(0, 1);
  split->setStretchFactor(1, 5);
  split->setStretchFactor(2, 1);
  split->setSizes({190, 1020, 190});
  setCentralWidget(split);
  setMinimumSize(960, 600);
  resize(1400, 860);
  statusBar()->showMessage(tr("Open a .trscn or capture the current view."));

  connect(inspector_, &RenderSceneInspector::overlay_requested, this,
          [this](Aabb box, quint64 node_id, bool isolate) {
            replay_->player().set_debug_aabb(box);
            replay_->player().set_isolate_node(isolate ? std::optional<std::uint64_t>{node_id}
                                                       : std::nullopt);
            replay_->sync_from_player();
            rebuild_commands();
          });
  connect(inspector_, &RenderSceneInspector::overlay_cleared, this, [this] {
    replay_->player().set_debug_aabb(std::nullopt);
    replay_->player().set_isolate_node(std::nullopt);
    replay_->sync_from_player();
    rebuild_commands();
  });
  connect(inspector_, &RenderSceneInspector::vertex_overlay_requested, this,
          [this](const DebugVertexOverlay& vertex) {
            replay_->player().set_debug_vertex(vertex);
            replay_->sync_from_player();
          });
  connect(inspector_, &RenderSceneInspector::vertex_overlay_cleared, this, [this] {
    replay_->player().set_debug_vertex(std::nullopt);
    replay_->sync_from_player();
  });
  connect(inspector_, &RenderSceneInspector::triangle_overlay_requested, this,
          [this](Vec3 v0, Vec3 v1, Vec3 v2) {
            replay_->player().set_debug_triangle(std::array<Vec3, 3>{v0, v1, v2});
            replay_->sync_from_player();
          });
  connect(inspector_, &RenderSceneInspector::triangle_overlay_cleared, this, [this] {
    replay_->player().set_debug_triangle(std::nullopt);
    replay_->sync_from_player();
  });
  connect(inspector_, &RenderSceneInspector::dump_selected_requested, this,
          &SceneDebuggerWindow::write_selected_draw);
  connect(replay_, &ReplayViewport::node_picked, this, [this](quint64 node_id) {
    if (node_id != 0) {
      inspector_->select_node(node_id);
    }
  });
}

SceneDebuggerWindow::~SceneDebuggerWindow() = default;

void SceneDebuggerWindow::open_scene(RenderScene scene, std::filesystem::path path,
                                     QPointer<DocumentViewport> source) {
  source_ = std::move(source);
  recapture_action_->setEnabled(source_ != nullptr);
  replay_->set_scene(scene);
  inspector_->show_scene(scene);
  set_source_path(std::move(path));
  sync_step_controls();
  switch (replay_->render_mode()) {
    case RenderMode::Wireframe:
      wireframe_action_->setChecked(true);
      break;
    case RenderMode::Shaded:
      shaded_action_->setChecked(true);
      break;
    case RenderMode::Realistic:
      realistic_action_->setChecked(true);
      break;
  }
  axes_action_->setChecked(replay_->player().show_axes());
  apply_hidden_action_->setChecked(replay_->player().apply_captured_hidden());
  rebuild_commands();
  show();
  raise();
  activateWindow();
}

bool SceneDebuggerWindow::is_for_path(const std::filesystem::path& path) const {
  return !path_.empty() && path_ == path;
}

void SceneDebuggerWindow::closeEvent(QCloseEvent* event) {
  QMainWindow::closeEvent(event);
}

void SceneDebuggerWindow::set_source_path(std::filesystem::path path) {
  path_ = std::move(path);
  if (path_.empty()) {
    setWindowTitle(tr("Scene Debugger"));
    return;
  }
  setWindowTitle(tr("Scene Debugger — %1").arg(path_to_qstring(path_.filename())));
}

void SceneDebuggerWindow::sync_step_controls() {
  const int n = replay_->player().has_scene()
                    ? static_cast<int>(replay_->player().scene().items.size())
                    : 0;
  QSignalBlocker block(step_slider_);
  step_slider_->setMinimum(0);
  step_slider_->setMaximum(n);
  step_slider_->setValue(n);
  if (n == 0) {
    step_label_->setText(tr("Draws: none"));
  } else {
    step_label_->setText(tr("Draws: all (%1)").arg(n));
  }
  replay_->player().set_step_count(std::nullopt);
}

void SceneDebuggerWindow::on_step_changed(int value) {
  const int n = static_cast<int>(replay_->player().scene().items.size());
  if (value <= 0) {
    replay_->player().set_step_count(0);
    step_label_->setText(tr("Draws: 0 / %1").arg(n));
  } else if (value >= n) {
    replay_->player().set_step_count(std::nullopt);
    step_label_->setText(tr("Draws: all (%1)").arg(n));
  } else {
    replay_->player().set_step_count(static_cast<std::size_t>(value));
    step_label_->setText(tr("Draws: %1 / %2").arg(value).arg(n));
  }
  replay_->sync_from_player();
  rebuild_commands();
}

void SceneDebuggerWindow::rebuild_commands() {
  if (!replay_->player().has_scene()) {
    commands_->setRowCount(0);
    commands_summary_->setText(tr("No scene loaded."));
    return;
  }
  const TurntableCamera& cam = replay_->camera().camera();
  const float aspect = static_cast<float>(std::max(1, replay_->width())) /
                       static_cast<float>(std::max(1, replay_->height()));
  const Mat4 view_proj = cam.proj_matrix(aspect) * cam.view_matrix();
  const Frustum frustum = Frustum::from_view_proj(view_proj);
  const SceneDebugLog log = capture_scene_debug_log(replay_->player(), &frustum);
  commands_summary_->setText(
      tr("Cargo: drawn %1  hidden %2  isolated %3  stepped %4  culled %5\n"
         "Recorded draw_indexed: %6")
          .arg(log.drawn_items)
          .arg(log.hidden)
          .arg(log.isolated)
          .arg(log.stepped)
          .arg(log.culled)
          .arg(log.draws.size()));

  commands_->setRowCount(static_cast<int>(log.items.size() + log.draws.size() + 1));
  int row = 0;
  auto set_row = [&](const QStringList& cols) {
    for (int c = 0; c < cols.size() && c < commands_->columnCount(); ++c) {
      auto* item = new QTableWidgetItem(cols[c]);
      item->setFlags(item->flags() & ~Qt::ItemIsEditable);
      commands_->setItem(row, c, item);
    }
    ++row;
  };
  for (const auto& item : log.items) {
    set_row({QString::number(item.index), QString::number(item.node_id),
             QString::number(item.mesh_asset_id), QString::number(item.triangles),
             skip_reason_label(item.reason), tr("item")});
  }
  set_row({QString(), tr("— recorded draws —"), QString(), QString(), QString(), QString()});
  for (const auto& draw : log.draws) {
    set_row({QString::number(draw.index), tr("draw_indexed"),
             QString::number(draw.index_count), QString::number(draw.instance_count),
             pipeline_label(draw.pipeline),
             draw.transparent ? tr("transparent") : tr("opaque")});
  }
  commands_->setRowCount(row);
}

void SceneDebuggerWindow::set_mode(RenderMode mode) { replay_->set_render_mode(mode); }

void SceneDebuggerWindow::open_file() {
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Open Render Scene"), QString(), tr("Render Scene (*.trscn)"));
  if (path.isEmpty()) {
    return;
  }
  auto loaded = load_render_scene(qstring_to_path(path));
  if (!loaded) {
    QMessageBox::critical(this, tr("Open"), QString::fromStdString(loaded.error()));
    return;
  }
  open_scene(std::move(*loaded), qstring_to_path(QFileInfo(path).absoluteFilePath()), {});
}

void SceneDebuggerWindow::save_snapshot() {
  if (!replay_->player().has_scene()) {
    QMessageBox::information(this, tr("Save"), tr("No scene loaded."));
    return;
  }
  QString suggested = path_.empty() ? QStringLiteral("scene.trscn") : path_to_qstring(path_);
  const QString path = QFileDialog::getSaveFileName(
      this, tr("Save Render Scene Snapshot"), suggested, tr("Render Scene (*.trscn)"));
  if (path.isEmpty()) {
    return;
  }
  QString out_path = path;
  if (QFileInfo(out_path).suffix().compare(QStringLiteral("trscn"), Qt::CaseInsensitive) != 0) {
    out_path += QStringLiteral(".trscn");
  }
  const auto file = qstring_to_path(QFileInfo(out_path).absoluteFilePath());
  const RenderScene& scene = replay_->player().scene();
  if (auto r = save_render_scene(file, scene); !r) {
    QMessageBox::critical(this, tr("Save"), QString::fromStdString(r.error()));
    return;
  }
  if (auto r = write_render_scene_debug_sidecars(file, scene); !r) {
    QMessageBox::warning(this, tr("Save"),
                         tr("Scene saved, but debug dump failed:\n%1")
                             .arg(QString::fromStdString(r.error())));
  }
  set_source_path(file);
  statusBar()->showMessage(
      tr("Saved %1  digest=%2")
          .arg(path_to_qstring(file), QString::fromStdString(render_scene_digest(scene))),
      8000);
  reveal_path(file.parent_path());
}

void SceneDebuggerWindow::pin_golden() {
  if (!replay_->player().has_scene()) {
    QMessageBox::information(this, tr("Pin"), tr("No scene loaded."));
    return;
  }
  const std::filesystem::path source_dir{TAMIAS_SOURCE_DIR};
  const auto root = render_scene_golden_root(source_dir);
  if (!std::filesystem::exists(source_dir)) {
    QMessageBox::warning(this, tr("Pin"),
                         tr("Source tree not found. Pin is for a local checkout:\n%1")
                             .arg(QString::fromUtf8(TAMIAS_SOURCE_DIR)));
    return;
  }
  const RenderScene& scene = replay_->player().scene();
  QString suggested = QString::fromStdString(suggest_render_scene_golden_slug(scene.source));
  bool ok = false;
  const QString slug_q = QInputDialog::getText(
      this, tr("Pin Render Scene for Tests"),
      tr("Fixture name (letters, digits, '-' '_'; saved under assets/samples/render/):"),
      QLineEdit::Normal, suggested, &ok);
  if (!ok || slug_q.trimmed().isEmpty()) {
    return;
  }
  const std::string slug = slug_q.trimmed().toStdString();
  if (!is_render_scene_golden_slug(slug)) {
    QMessageBox::warning(
        this, tr("Pin"),
        tr("Name must start with a letter and use only A–Z, a–z, 0–9, '-' or '_'."));
    return;
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
      return;
    }
    overwrite = true;
  }
  auto meta = save_render_scene_golden(root, slug, scene, overwrite);
  if (!meta) {
    QMessageBox::critical(this, tr("Pin"), QString::fromStdString(meta.error()));
    return;
  }
  const GoldenTestRun tests = run_render_scene_golden_tests(source_dir, this);
  QString inspect = tr("Full dump: %1\nMeshes / textures: %2\n\n")
                        .arg(path_to_qstring(dir / "scene.inspect.txt"),
                             path_to_qstring(dir / "debug"));
  inspect += QStringLiteral("--- RenderSceneGolden* ---\n");
  inspect += tests.log;
  if (tests.ok) {
    statusBar()->showMessage(
        tr("Pinned golden %1  digest=%2")
            .arg(path_to_qstring(dir), QString::fromStdString(meta->digest)),
        8000);
  } else {
    statusBar()->showMessage(
        tr("Pinned %1 but RenderSceneGolden* failed").arg(path_to_qstring(dir)), 12000);
  }
  PinResultDialog dlg(scene, path_to_qstring(dir), inspect, tests, this);
  dlg.exec();
}

void SceneDebuggerWindow::write_debug_files() {
  if (!replay_->player().has_scene()) {
    QMessageBox::information(this, tr("Debug files"), tr("No scene loaded."));
    return;
  }
  const RenderScene& scene = replay_->player().scene();
  std::filesystem::path target = path_;
  if (target.empty()) {
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Write debug files"));
    if (dir.isEmpty()) {
      return;
    }
    target = qstring_to_path(dir) / "scene.trscn";
    if (auto r = save_render_scene(target, scene); !r) {
      QMessageBox::critical(this, tr("Debug files"), QString::fromStdString(r.error()));
      return;
    }
    set_source_path(target);
  }
  if (auto r = write_render_scene_debug_sidecars(target, scene); !r) {
    QMessageBox::critical(this, tr("Debug files"), QString::fromStdString(r.error()));
    return;
  }
  std::filesystem::path debug = target;
  debug.replace_extension(".debug");
  statusBar()->showMessage(tr("Wrote sidecars next to %1").arg(path_to_qstring(target)), 8000);
  reveal_path(debug);
}

void SceneDebuggerWindow::write_selected_draw() {
  if (!replay_->player().has_scene()) {
    return;
  }
  const int index = inspector_->current_draw_index();
  const RenderScene& scene = replay_->player().scene();
  if (index < 0 || index >= static_cast<int>(scene.items.size())) {
    QMessageBox::information(this, tr("OBJ"), tr("Select a draw first."));
    return;
  }
  const SceneDrawItem& item = scene.items[static_cast<std::size_t>(index)];
  const QString suggested =
      QStringLiteral("draw_%1_node_%2.obj").arg(index).arg(item.node_id);
  const QString path = QFileDialog::getSaveFileName(this, tr("Write this draw as OBJ"), suggested,
                                                    tr("Wavefront OBJ (*.obj)"));
  if (path.isEmpty()) {
    return;
  }
  if (auto r = write_render_scene_debug_draw(qstring_to_path(path), scene,
                                             static_cast<std::size_t>(index));
      !r) {
    QMessageBox::critical(this, tr("OBJ"), QString::fromStdString(r.error()));
    return;
  }
  statusBar()->showMessage(tr("Wrote %1").arg(path), 8000);
}

void SceneDebuggerWindow::recapture() {
  if (source_ == nullptr) {
    QMessageBox::information(this, tr("Recapture"),
                             tr("This session is from a file. Open a document and capture again."));
    return;
  }
  open_scene(source_->capture_debug_scene(), path_, source_);
}

void SceneDebuggerWindow::compare_with_file() {
  if (!replay_->player().has_scene()) {
    QMessageBox::information(this, tr("Compare"), tr("No scene loaded."));
    return;
  }
  const QString path = QFileDialog::getOpenFileName(
      this, tr("Compare with Render Scene"), QString(), tr("Render Scene (*.trscn)"));
  if (path.isEmpty()) {
    return;
  }
  auto loaded = load_render_scene(qstring_to_path(path));
  if (!loaded) {
    QMessageBox::critical(this, tr("Compare"), QString::fromStdString(loaded.error()));
    return;
  }
  const QString text =
      compare_scenes(replay_->player().scene(), *loaded, tr("Current"), QFileInfo(path).fileName());
  QDialog dlg(this);
  dlg.setWindowTitle(tr("Compare scenes"));
  dlg.resize(640, 400);
  auto* layout = new QVBoxLayout(&dlg);
  auto* edit = new QPlainTextEdit(&dlg);
  edit->setReadOnly(true);
  edit->setPlainText(text);
  layout->addWidget(edit);
  dlg.exec();
}

}  // namespace tamias
