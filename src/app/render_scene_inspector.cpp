#include "render_scene_inspector.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <vector>

namespace tamias {
namespace {

QString fmt_vec(const Vec3& v, int digits = 4) {
  return QStringLiteral("(%1, %2, %3)")
      .arg(v.x, 0, 'g', digits)
      .arg(v.y, 0, 'g', digits)
      .arg(v.z, 0, 'g', digits);
}

QString fmt_aabb(const Aabb& box) {
  if (!box.valid()) {
    return QStringLiteral("—");
  }
  return fmt_vec(box.min) + QStringLiteral("  →  ") + fmt_vec(box.max);
}

QString mode_name(const RenderSceneInspector& self, RenderMode mode) {
  switch (mode) {
    case RenderMode::Wireframe:
      return self.tr("Wireframe");
    case RenderMode::Shaded:
      return self.tr("Shaded");
    case RenderMode::Realistic:
      return self.tr("Realistic");
  }
  return QStringLiteral("?");
}

QColor vec_color(const Vec3& c) {
  const auto clamp01 = [](float v) {
    return std::clamp(static_cast<int>(v * 255.f + 0.5f), 0, 255);
  };
  return QColor(clamp01(c.x), clamp01(c.y), clamp01(c.z));
}

QLabel* make_value(QWidget* parent) {
  auto* label = new QLabel(parent);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setWordWrap(true);
  label->setText(QStringLiteral("—"));
  return label;
}

QLabel* make_chip(QWidget* parent) {
  auto* chip = new QLabel(parent);
  chip->setFixedSize(18, 18);
  chip->setFrameShape(QFrame::Box);
  chip->setLineWidth(1);
  return chip;
}

QTableWidget* make_table(QWidget* parent, const QStringList& headers) {
  auto* table = new QTableWidget(parent);
  table->setColumnCount(headers.size());
  table->setHorizontalHeaderLabels(headers);
  table->horizontalHeader()->setStretchLastSection(true);
  table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
  table->verticalHeader()->setVisible(false);
  table->setSelectionBehavior(QAbstractItemView::SelectRows);
  table->setSelectionMode(QAbstractItemView::SingleSelection);
  table->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table->setAlternatingRowColors(true);
  table->setShowGrid(false);
  table->setWordWrap(false);
  table->verticalHeader()->setDefaultSectionSize(26);
  return table;
}

void set_cell(QTableWidget* table, int row, int col, const QString& text) {
  auto* item = new QTableWidgetItem(text);
  item->setFlags(item->flags() & ~Qt::ItemIsEditable);
  table->setItem(row, col, item);
}

std::uint64_t mesh_tris(const MeshCpu& mesh) {
  if (mesh.line_list) {
    return 0;
  }
  return static_cast<std::uint64_t>(mesh.indices.size() / 3);
}

}  // namespace

RenderSceneInspector::RenderSceneInspector(QWidget* parent) : QWidget(parent) {
  setMinimumWidth(200);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  empty_ = new QLabel(
      tr("Open a document to capture the current view's draw list."), this);
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  empty_->setMinimumHeight(120);
  root->addWidget(empty_);

  body_ = new QWidget(this);
  auto* body_layout = new QVBoxLayout(body_);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(8);

  auto* summary = new QGroupBox(tr("Scene"), body_);
  auto* summary_layout = new QVBoxLayout(summary);
  summary_mode_ = make_value(summary);
  {
    QFont font = summary_mode_->font();
    font.setBold(true);
    font.setPointSizeF(font.pointSizeF() + 1.5);
    summary_mode_->setFont(font);
  }
  summary_counts_ = make_value(summary);
  summary_digest_ = make_value(summary);
  summary_camera_ = make_value(summary);
  summary_camera2_ = make_value(summary);
  summary_source_ = make_value(summary);
  summary_layout->addWidget(summary_mode_);
  summary_layout->addWidget(summary_counts_);
  summary_layout->addWidget(summary_digest_);
  summary_layout->addWidget(summary_camera_);
  summary_layout->addWidget(summary_camera2_);
  summary_layout->addWidget(summary_source_);

  auto* actions = new QWidget(summary);
  actions_ = actions;
  auto* actions_layout = new QGridLayout(actions);
  actions_layout->setContentsMargins(0, 4, 0, 0);
  actions_layout->setHorizontalSpacing(6);
  actions_layout->setVerticalSpacing(6);
  auto* refresh_btn = new QPushButton(tr("Refresh"), actions);
  refresh_btn->setToolTip(tr("Recapture the current viewport's cooked draw list"));
  auto* save_btn = new QPushButton(tr("Save snapshot…"), actions);
  save_btn->setToolTip(
      tr("Write a .trscn plus inspect.txt / OBJ / PPM. Does not change the current document."));
  auto* pin_btn = new QPushButton(tr("Pin for tests…"), actions);
  pin_btn->setToolTip(
      tr("Write assets/samples/render/<name>/ and run RenderSceneGolden*"));
  auto* dump_btn = new QPushButton(tr("Write debug files…"), actions);
  dump_btn->setToolTip(
      tr("Write inspect.txt, OBJ meshes, and PPM textures. From a live view, also writes scene.trscn."));
  connect(refresh_btn, &QPushButton::clicked, this, &RenderSceneInspector::refresh_requested);
  connect(save_btn, &QPushButton::clicked, this, &RenderSceneInspector::save_requested);
  connect(pin_btn, &QPushButton::clicked, this, &RenderSceneInspector::pin_requested);
  connect(dump_btn, &QPushButton::clicked, this, &RenderSceneInspector::dump_requested);
  actions_layout->addWidget(refresh_btn, 0, 0);
  actions_layout->addWidget(save_btn, 0, 1);
  actions_layout->addWidget(pin_btn, 1, 0);
  actions_layout->addWidget(dump_btn, 1, 1);
  summary_layout->addWidget(actions);
  body_layout->addWidget(summary);

  auto* split = new QSplitter(Qt::Vertical, body_);
  split->setChildrenCollapsible(false);

  auto* list_box = new QGroupBox(tr("Draws"), split);
  auto* list_layout = new QVBoxLayout(list_box);
  list_layout->setContentsMargins(8, 8, 8, 8);
  filter_ = new QLineEdit(list_box);
  filter_->setPlaceholderText(tr("Filter by #, node, or mesh id"));
  filter_->setClearButtonEnabled(true);
  connect(filter_, &QLineEdit::textChanged, this, &RenderSceneInspector::apply_filter);
  list_layout->addWidget(filter_);
  draws_ = make_table(list_box, {tr("#"), tr("Node"), tr("Tris"), tr("Color"), tr("Albedo")});
  draws_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
  draws_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
  draws_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
  draws_->setColumnWidth(3, 48);
  list_layout->addWidget(draws_);
  connect(draws_, &QTableWidget::itemSelectionChanged, this,
          &RenderSceneInspector::on_draw_selected);
  split->addWidget(list_box);

  auto* detail_scroll = new QScrollArea(split);
  detail_scroll->setWidgetResizable(true);
  detail_scroll->setFrameShape(QFrame::NoFrame);
  auto* detail_box = new QGroupBox(tr("Selected draw"), detail_scroll);
  auto* detail_layout = new QVBoxLayout(detail_box);

  isolate_ = new QCheckBox(tr("Isolate this draw"), detail_box);
  connect(isolate_, &QCheckBox::toggled, this, [this](bool) { emit_overlay(); });
  detail_layout->addWidget(isolate_);
  auto* dump_draw_btn = new QPushButton(tr("Write this draw as OBJ…"), detail_box);
  dump_draw_btn->setToolTip(tr("World-space mesh of the selected draw, for Blender or any DCC"));
  connect(dump_draw_btn, &QPushButton::clicked, this,
          &RenderSceneInspector::dump_selected_requested);
  detail_layout->addWidget(dump_draw_btn);

  auto* form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setHorizontalSpacing(12);
  form->setVerticalSpacing(6);
  field_node_ = make_value(detail_box);
  field_mesh_ = make_value(detail_box);
  field_flags_ = make_value(detail_box);
  field_aabb_ = make_value(detail_box);
  field_translation_ = make_value(detail_box);
  field_pbr_ = make_value(detail_box);
  field_tex_ = make_value(detail_box);
  field_transform_ = new QTableWidget(4, 4, detail_box);
  field_transform_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  field_transform_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  field_transform_->setSelectionMode(QAbstractItemView::NoSelection);
  field_transform_->setFocusPolicy(Qt::NoFocus);
  field_transform_->horizontalHeader()->setVisible(false);
  field_transform_->verticalHeader()->setVisible(false);
  field_transform_->setShowGrid(false);
  field_transform_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  field_transform_->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  field_transform_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  for (int c = 0; c < 4; ++c) {
    field_transform_->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
  }
  field_transform_->verticalHeader()->setDefaultSectionSize(
      QFontMetrics(field_transform_->font()).lineSpacing() + 4);
  field_transform_->setFixedHeight(field_transform_->verticalHeader()->defaultSectionSize() * 4 + 4);
  form->addRow(tr("Node"), field_node_);
  form->addRow(tr("Mesh"), field_mesh_);
  form->addRow(tr("Flags"), field_flags_);
  form->addRow(tr("AABB"), field_aabb_);
  form->addRow(tr("Translation"), field_translation_);
  form->addRow(tr("PBR"), field_pbr_);
  form->addRow(tr("Texture"), field_tex_);
  form->addRow(tr("Transform"), field_transform_);
  detail_layout->addLayout(form);

  auto* colors = new QHBoxLayout();
  auto* color_block = new QVBoxLayout();
  auto* color_row = new QHBoxLayout();
  color_chip_ = make_chip(detail_box);
  category_chip_ = make_chip(detail_box);
  color_row->addWidget(color_chip_);
  color_row->addWidget(new QLabel(tr("Base"), detail_box));
  color_row->addSpacing(12);
  color_row->addWidget(category_chip_);
  color_row->addWidget(new QLabel(tr("Category"), detail_box));
  color_row->addStretch();
  color_block->addLayout(color_row);
  colors->addLayout(color_block);
  detail_layout->addLayout(colors);

  auto* tex_row = new QHBoxLayout();
  auto add_tex = [&](QLabel*& image, QLabel*& caption) {
    auto* col = new QVBoxLayout();
    image = new QLabel(detail_box);
    image->setFixedSize(96, 96);
    image->setAlignment(Qt::AlignCenter);
    image->setFrameShape(QFrame::StyledPanel);
    image->setScaledContents(false);
    caption = new QLabel(detail_box);
    caption->setAlignment(Qt::AlignCenter);
    caption->setWordWrap(true);
    col->addWidget(image, 0, Qt::AlignHCenter);
    col->addWidget(caption);
    tex_row->addLayout(col);
  };
  add_tex(albedo_image_, albedo_caption_);
  add_tex(normal_image_, normal_caption_);
  add_tex(orm_image_, orm_caption_);
  tex_row->addStretch();
  detail_layout->addLayout(tex_row);
  detail_layout->addStretch();
  detail_scroll->setWidget(detail_box);
  split->addWidget(detail_scroll);

  auto* extras = new QTabWidget(split);
  extras_ = extras;
  verts_page_ = new QWidget(extras);
  auto* verts_layout = new QVBoxLayout(verts_page_);
  verts_layout->setContentsMargins(0, 0, 0, 0);
  verts_layout->setSpacing(6);
  verts_ = make_table(verts_page_, {tr("#"), tr("Position"), tr("Normal"), tr("UV"), tr("Color")});
  verts_->setToolTip(tr("Click a row to mark that vertex in the viewport."));
  connect(verts_, &QTableWidget::itemSelectionChanged, this,
          &RenderSceneInspector::on_vertex_selected);
  connect(verts_, &QTableWidget::cellClicked, this,
          [this](int, int) { on_vertex_selected(); });
  auto* legend = new QLabel(
      tr("Magenta diamond: position  ·  RGB axes: XYZ  ·  Cyan arrow: normal  ·  "
         "Red/green ticks: UV (length = value)  ·  Inner diamond: vertex color"),
      verts_page_);
  legend->setWordWrap(true);
  legend->setStyleSheet(QStringLiteral("color: palette(mid);"));
  verts_layout->addWidget(verts_, 1);
  verts_layout->addWidget(legend);
  extras->addTab(verts_page_, tr("Vertices"));

  tris_page_ = new QWidget(extras);
  auto* tris_layout = new QVBoxLayout(tris_page_);
  tris_layout->setContentsMargins(0, 0, 0, 0);
  tris_layout->setSpacing(6);
  tris_ = make_table(tris_page_, {tr("#"), tr("v0"), tr("v1"), tr("v2"), tr("Face normal"), tr("Area")});
  tris_->setToolTip(tr("Click a row to highlight that triangle in the viewport."));
  connect(tris_, &QTableWidget::itemSelectionChanged, this,
          &RenderSceneInspector::on_triangle_selected);
  connect(tris_, &QTableWidget::cellClicked, this,
          [this](int, int) { on_triangle_selected(); });
  auto* tris_hint = new QLabel(
      tr("Face normal and area use local-space vertex positions; highlight uses world space."),
      tris_page_);
  tris_hint->setWordWrap(true);
  tris_hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
  tris_layout->addWidget(tris_, 1);
  tris_layout->addWidget(tris_hint);
  extras->addTab(tris_page_, tr("Triangles"));

  faces_page_ = new QWidget(extras);
  auto* faces_layout = new QVBoxLayout(faces_page_);
  faces_layout->setContentsMargins(0, 0, 0, 0);
  faces_layout->setSpacing(6);
  faces_ = make_table(faces_page_,
                      {tr("#"), tr("first_index"), tr("index_count"), tr("tris"), tr("Bounds")});
  faces_->setToolTip(tr("Click a row to highlight that BRep face's AABB in the viewport."));
  connect(faces_, &QTableWidget::itemSelectionChanged, this,
          &RenderSceneInspector::on_face_selected);
  connect(faces_, &QTableWidget::cellClicked, this,
          [this](int, int) { on_face_selected(); });
  auto* faces_hint = new QLabel(
      tr("BRep face index ranges; empty when the mesh is an imported triangle soup."),
      faces_page_);
  faces_hint->setWordWrap(true);
  faces_hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
  faces_layout->addWidget(faces_, 1);
  faces_layout->addWidget(faces_hint);
  extras->addTab(faces_page_, tr("Faces"));

  dump_ = new QPlainTextEdit(extras);
  dump_->setReadOnly(true);
  dump_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  extras->addTab(dump_, tr("Dump"));
  split->addWidget(extras);

  split->setStretchFactor(0, 3);
  split->setStretchFactor(1, 3);
  split->setStretchFactor(2, 2);
  split->setSizes({180, 280, 160});
  body_layout->addWidget(split, 1);

  root->addWidget(body_, 1);
  body_->hide();
}

void RenderSceneInspector::show_scene(const RenderScene& scene) {
  scene_ = scene;
  has_scene_ = true;
  if (isolate_ != nullptr) {
    QSignalBlocker block(isolate_);
    isolate_->setChecked(false);
  }
  rebuild();
}

void RenderSceneInspector::clear() {
  scene_ = {};
  has_scene_ = false;
  rebuild();
  emit overlay_cleared();
  emit vertex_overlay_cleared();
}

void RenderSceneInspector::set_actions_visible(bool visible) {
  if (actions_ != nullptr) {
    actions_->setVisible(visible);
  }
}

void RenderSceneInspector::select_node(quint64 node_id) {
  if (!has_scene_ || draws_ == nullptr) {
    return;
  }
  for (int i = 0; i < static_cast<int>(scene_.items.size()); ++i) {
    if (scene_.items[static_cast<std::size_t>(i)].node_id == node_id) {
      if (draws_->isRowHidden(i) && filter_ != nullptr) {
        filter_->clear();
      }
      draws_->selectRow(i);
      if (auto* item = draws_->item(i, 0)) {
        draws_->scrollToItem(item);
      }
      return;
    }
  }
}

int RenderSceneInspector::current_draw_index() const {
  if (!has_scene_ || draws_ == nullptr) {
    return -1;
  }
  const int row = draws_->currentRow();
  if (row < 0 || row >= static_cast<int>(scene_.items.size())) {
    return -1;
  }
  return row;
}

void RenderSceneInspector::show_empty() {
  empty_->show();
  body_->hide();
  draws_->setRowCount(0);
  verts_->setRowCount(0);
  tris_->setRowCount(0);
  faces_->setRowCount(0);
  dump_->clear();
  set_vertices_tab_title(-1);
  set_triangles_tab_title(-1);
  set_faces_tab_title(-1);
}

void RenderSceneInspector::rebuild() {
  if (!has_scene_) {
    show_empty();
    return;
  }
  empty_->hide();
  body_->show();

  const QString digest = QString::fromStdString(render_scene_digest(scene_));
  summary_mode_->setText(
      tr("%1  ·  %2×%3")
          .arg(mode_name(*this, scene_.view.mode))
          .arg(scene_.view.width)
          .arg(scene_.view.height));
  summary_counts_->setText(tr("%1 draws · %2 meshes · %3 textures · %4 tris · %5 hidden")
                               .arg(scene_.items.size())
                               .arg(scene_.meshes.size())
                               .arg(scene_.textures.size())
                               .arg(render_scene_triangle_count(scene_))
                               .arg(scene_.hidden_node_ids.size()));
  summary_digest_->setText(tr("digest  %1").arg(digest));
  summary_camera_->setText(tr("eye %1\ntarget %2\ndistance %3")
                               .arg(fmt_vec(scene_.view.eye_position), fmt_vec(scene_.view.target))
                               .arg(scene_.view.view_distance, 0, 'g', 4));
  summary_camera2_->setText(
      tr("yaw %1  ·  pitch %2  ·  fovy %3  ·  znear %4  ·  zfar %5  ·  %6")
          .arg(scene_.view.yaw, 0, 'g', 4)
          .arg(scene_.view.pitch, 0, 'g', 4)
          .arg(scene_.view.fovy, 0, 'g', 4)
          .arg(scene_.view.znear, 0, 'g', 4)
          .arg(scene_.view.zfar, 0, 'g', 4)
          .arg(scene_.view.orthographic ? tr("orthographic") : tr("perspective")));
  const QString source_name = QString::fromStdString(scene_.source);
  summary_source_->setText(tr("source %1  ·  v%2")
                                .arg(source_name.isEmpty() ? tr("(unnamed)") : source_name)
                                .arg(scene_.version));
  dump_->setPlainText(QString::fromStdString(inspect_render_scene(scene_)));

  int select = draws_->currentRow();
  {
    const QSignalBlocker block(draws_);
    draws_->setRowCount(static_cast<int>(scene_.items.size()));
    for (int i = 0; i < draws_->rowCount(); ++i) {
      const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(i)];
      std::uint64_t tris = 0;
      const auto it = scene_.meshes.find(item.mesh_asset_id);
      if (it != scene_.meshes.end()) {
        tris = mesh_tris(it->second);
      }
      set_cell(draws_, i, 0, QString::number(i));
      set_cell(draws_, i, 1, QString::number(item.node_id));
      set_cell(draws_, i, 2, QString::number(tris));
      auto* color_item = new QTableWidgetItem();
      color_item->setFlags(color_item->flags() & ~Qt::ItemIsEditable);
      color_item->setBackground(vec_color(item.color));
      color_item->setToolTip(fmt_vec(item.color));
      draws_->setItem(i, 3, color_item);
      set_cell(draws_, i, 4,
               item.albedo_texture_id ? QString::number(item.albedo_texture_id)
                                      : QStringLiteral("—"));
    }
    if (select < 0 || select >= draws_->rowCount()) {
      select = draws_->rowCount() > 0 ? 0 : -1;
    }
    if (select >= 0) {
      draws_->selectRow(select);
    }
  }
  apply_filter();
  if (select >= 0) {
    fill_selected_draw(select);
    emit_overlay();
  } else {
    fill_selected_draw(-1);
    emit overlay_cleared();
  }
  emit vertex_overlay_cleared();
  emit triangle_overlay_cleared();
}

void RenderSceneInspector::apply_filter() {
  if (draws_ == nullptr) {
    return;
  }
  const QString q = filter_ != nullptr ? filter_->text().trimmed() : QString();
  for (int i = 0; i < draws_->rowCount(); ++i) {
    bool match = q.isEmpty();
    if (!match && i < static_cast<int>(scene_.items.size())) {
      const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(i)];
      match = QString::number(i).contains(q, Qt::CaseInsensitive) ||
              QString::number(item.node_id).contains(q, Qt::CaseInsensitive) ||
              QString::number(item.mesh_asset_id).contains(q, Qt::CaseInsensitive);
    }
    draws_->setRowHidden(i, !match);
  }
}

void RenderSceneInspector::on_draw_selected() {
  if (!has_scene_ || draws_->currentRow() < 0) {
    fill_selected_draw(-1);
    emit overlay_cleared();
    emit vertex_overlay_cleared();
    emit triangle_overlay_cleared();
    return;
  }
  fill_selected_draw(draws_->currentRow());
  emit_overlay();
  emit vertex_overlay_cleared();
  emit triangle_overlay_cleared();
}

void RenderSceneInspector::on_vertex_selected() {
  emit_vertex_overlay();
}

void RenderSceneInspector::fill_selected_draw(int index) {
  if (index < 0 || index >= static_cast<int>(scene_.items.size())) {
    field_node_->setText(QStringLiteral("—"));
    field_mesh_->setText(QStringLiteral("—"));
    field_flags_->setText(QStringLiteral("—"));
    field_aabb_->setText(QStringLiteral("—"));
    field_translation_->setText(QStringLiteral("—"));
    field_pbr_->setText(QStringLiteral("—"));
    field_tex_->setText(QStringLiteral("—"));
    field_transform_->clearContents();
    set_color_chip(color_chip_, {0.5f, 0.5f, 0.5f});
    set_color_chip(category_chip_, {0.5f, 0.5f, 0.5f});
    set_texture_thumb(albedo_image_, albedo_caption_, 0, tr("Albedo"));
    set_texture_thumb(normal_image_, normal_caption_, 0, tr("Normal"));
    set_texture_thumb(orm_image_, orm_caption_, 0, tr("ORM"));
    verts_->setRowCount(0);
    tris_->setRowCount(0);
    faces_->setRowCount(0);
    set_vertices_tab_title(-1);
    set_triangles_tab_title(-1);
    set_faces_tab_title(-1);
    return;
  }

  const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(index)];
  std::uint64_t verts = 0;
  std::uint64_t tris = 0;
  bool lines = item.lines;
  bool uv = false;
  const auto mesh_it = scene_.meshes.find(item.mesh_asset_id);
  if (mesh_it != scene_.meshes.end()) {
    verts = static_cast<std::uint64_t>(mesh_it->second.vertices.size());
    tris = mesh_tris(mesh_it->second);
    lines = lines || mesh_it->second.line_list;
    uv = mesh_it->second.has_texcoord;
  }

  field_node_->setText(QString::number(item.node_id));
  field_mesh_->setText(tr("id %1  ·  %2 verts  ·  %3 tris")
                           .arg(item.mesh_asset_id)
                           .arg(verts)
                           .arg(tris));
  QStringList flags;
  if (item.selected) {
    flags << tr("selected");
  }
  if (lines) {
    flags << tr("lines");
  }
  if (uv) {
    flags << tr("uv");
  }
  field_flags_->setText(flags.isEmpty() ? tr("none") : flags.join(QStringLiteral(" · ")));
  field_aabb_->setText(fmt_aabb(item.bounds));
  field_translation_->setText(fmt_vec(
      {item.transform(0, 3), item.transform(1, 3), item.transform(2, 3)}));
  field_pbr_->setText(tr("roughness %1  ·  metallic %2  ·  opacity %3")
                          .arg(item.roughness, 0, 'g', 4)
                          .arg(item.metallic, 0, 'g', 4)
                          .arg(item.opacity, 0, 'g', 4));
  field_tex_->setText(tr("scale (%1, %2)  ·  offset (%3, %4)  ·  rot %5 rad  ·  world_scale %6")
                          .arg(item.tex.scale.x, 0, 'g', 4)
                          .arg(item.tex.scale.y, 0, 'g', 4)
                          .arg(item.tex.offset.x, 0, 'g', 4)
                          .arg(item.tex.offset.y, 0, 'g', 4)
                          .arg(item.tex.rotation, 0, 'g', 4)
                          .arg(item.tex.world_scale, 0, 'g', 4));
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      auto* cell = new QTableWidgetItem(QString::number(item.transform(r, c), 'g', 4));
      cell->setFlags(cell->flags() & ~Qt::ItemIsEditable);
      cell->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
      field_transform_->setItem(r, c, cell);
    }
  }
  set_color_chip(color_chip_, item.color);
  set_color_chip(category_chip_, item.category_color);
  color_chip_->setToolTip(fmt_vec(item.color));
  category_chip_->setToolTip(fmt_vec(item.category_color));
  set_texture_thumb(albedo_image_, albedo_caption_, item.albedo_texture_id, tr("Albedo"));
  set_texture_thumb(normal_image_, normal_caption_, item.normal_texture_id, tr("Normal"));
  set_texture_thumb(orm_image_, orm_caption_, item.orm_texture_id, tr("ORM"));
  fill_vertices(item.mesh_asset_id);
  fill_triangles(item.mesh_asset_id);
  fill_faces(item.mesh_asset_id);
}

void RenderSceneInspector::set_vertices_tab_title(int count) {
  if (extras_ == nullptr) {
    return;
  }
  const int index = extras_->indexOf(verts_page_ ? verts_page_ : static_cast<QWidget*>(verts_));
  if (index < 0) {
    return;
  }
  if (count < 0) {
    extras_->setTabText(index, tr("Vertices"));
    return;
  }
  extras_->setTabText(index, tr("Vertices (%1)").arg(count));
}

void RenderSceneInspector::fill_vertices(std::uint64_t mesh_id) {
  const QSignalBlocker block(verts_);
  verts_->setRowCount(0);
  const auto it = scene_.meshes.find(mesh_id);
  if (it == scene_.meshes.end()) {
    verts_->setRowCount(1);
    set_cell(verts_, 0, 0, QStringLiteral("—"));
    set_cell(verts_, 0, 1, tr("Mesh %1 is not in this scene").arg(mesh_id));
    set_vertices_tab_title(0);
    return;
  }
  const MeshCpu& mesh = it->second;
  constexpr int kMax = 4000;
  const int total = static_cast<int>(mesh.vertices.size());
  const int n = std::min(total, kMax);
  verts_->setRowCount(n);
  for (int i = 0; i < n; ++i) {
    const Vertex& v = mesh.vertices[static_cast<std::size_t>(i)];
    set_cell(verts_, i, 0, QString::number(i));
    set_cell(verts_, i, 1, fmt_vec(v.position, 6));
    set_cell(verts_, i, 2, fmt_vec(v.normal, 4));
    set_cell(verts_, i, 3, QStringLiteral("%1, %2").arg(v.uv.x, 0, 'g', 4).arg(v.uv.y, 0, 'g', 4));
    set_cell(verts_, i, 4, fmt_vec(v.color, 3));
  }
  set_vertices_tab_title(total);
}

void RenderSceneInspector::set_triangles_tab_title(int count) {
  if (extras_ == nullptr || tris_page_ == nullptr) {
    return;
  }
  const int index = extras_->indexOf(tris_page_);
  if (index < 0) {
    return;
  }
  if (count < 0) {
    extras_->setTabText(index, tr("Triangles"));
    return;
  }
  extras_->setTabText(index, tr("Triangles (%1)").arg(count));
}

void RenderSceneInspector::set_faces_tab_title(int count) {
  if (extras_ == nullptr || faces_page_ == nullptr) {
    return;
  }
  const int index = extras_->indexOf(faces_page_);
  if (index < 0) {
    return;
  }
  if (count < 0) {
    extras_->setTabText(index, tr("Faces"));
    return;
  }
  extras_->setTabText(index, tr("Faces (%1)").arg(count));
}

void RenderSceneInspector::fill_triangles(std::uint64_t mesh_id) {
  const QSignalBlocker block(tris_);
  tris_->setRowCount(0);
  const auto it = scene_.meshes.find(mesh_id);
  if (it == scene_.meshes.end()) {
    tris_->setRowCount(1);
    set_cell(tris_, 0, 0, QStringLiteral("—"));
    set_cell(tris_, 0, 1, tr("Mesh %1 is not in this scene").arg(mesh_id));
    set_triangles_tab_title(0);
    return;
  }
  const MeshCpu& mesh = it->second;
  if (mesh.line_list) {
    tris_->setRowCount(1);
    set_cell(tris_, 0, 0, QStringLiteral("—"));
    set_cell(tris_, 0, 1, tr("Line list, no triangles"));
    set_triangles_tab_title(0);
    return;
  }
  const std::size_t idx_count = mesh.indices.size();
  const std::size_t total = idx_count / 3;
  constexpr int kMax = 4000;
  const int n = static_cast<int>(std::min(total, static_cast<std::size_t>(kMax)));
  tris_->setRowCount(n);
  for (int i = 0; i < n; ++i) {
    const std::size_t a = mesh.indices[static_cast<std::size_t>(i) * 3 + 0];
    const std::size_t b = mesh.indices[static_cast<std::size_t>(i) * 3 + 1];
    const std::size_t c = mesh.indices[static_cast<std::size_t>(i) * 3 + 2];
    set_cell(tris_, i, 0, QString::number(i));
    set_cell(tris_, i, 1, QString::number(a));
    set_cell(tris_, i, 2, QString::number(b));
    set_cell(tris_, i, 3, QString::number(c));
    if (a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size()) {
      const Vec3 p0 = mesh.vertices[a].position;
      const Vec3 p1 = mesh.vertices[b].position;
      const Vec3 p2 = mesh.vertices[c].position;
      const Vec3 e1 = p1 - p0;
      const Vec3 e2 = p2 - p0;
      const Vec3 cr = cross(e1, e2);
      const float area = 0.5f * length(cr);
      const Vec3 normal = normalize(cr);
      set_cell(tris_, i, 4, fmt_vec(normal, 4));
      set_cell(tris_, i, 5, QString::number(area, 'g', 4));
    } else {
      set_cell(tris_, i, 4, QStringLiteral("—"));
      set_cell(tris_, i, 5, QStringLiteral("—"));
    }
  }
  set_triangles_tab_title(static_cast<int>(total));
}

void RenderSceneInspector::fill_faces(std::uint64_t mesh_id) {
  const QSignalBlocker block(faces_);
  faces_->setRowCount(0);
  const auto it = scene_.meshes.find(mesh_id);
  if (it == scene_.meshes.end()) {
    faces_->setRowCount(1);
    set_cell(faces_, 0, 0, QStringLiteral("—"));
    set_cell(faces_, 0, 1, tr("Mesh %1 is not in this scene").arg(mesh_id));
    set_faces_tab_title(0);
    return;
  }
  const MeshCpu& mesh = it->second;
  if (mesh.faces.empty()) {
    faces_->setRowCount(1);
    set_cell(faces_, 0, 0, QStringLiteral("—"));
    set_cell(faces_, 0, 1, tr("No BRep face info (imported triangle soup)"));
    set_faces_tab_title(0);
    return;
  }
  const int total = static_cast<int>(mesh.faces.size());
  faces_->setRowCount(total);
  for (int i = 0; i < total; ++i) {
    const MeshFaceRange& face = mesh.faces[static_cast<std::size_t>(i)];
    set_cell(faces_, i, 0, QString::number(i));
    set_cell(faces_, i, 1, QString::number(face.first_index));
    set_cell(faces_, i, 2, QString::number(face.index_count));
    set_cell(faces_, i, 3, QString::number(face.index_count / 3));
    set_cell(faces_, i, 4, fmt_aabb(face.bounds));
  }
  set_faces_tab_title(total);
}

void RenderSceneInspector::on_triangle_selected() {
  emit_triangle_overlay();
}

void RenderSceneInspector::on_face_selected() {
  if (!has_scene_ || draws_ == nullptr || draws_->currentRow() < 0 ||
      draws_->currentRow() >= static_cast<int>(scene_.items.size()) || faces_ == nullptr) {
    emit_overlay();
    return;
  }
  const int row = faces_->currentRow();
  if (row < 0) {
    // 没选面，恢复选中 draw 的 AABB overlay。
    emit_overlay();
    return;
  }
  const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(draws_->currentRow())];
  const auto it = scene_.meshes.find(item.mesh_asset_id);
  if (it == scene_.meshes.end() || row >= static_cast<int>(it->second.faces.size())) {
    emit_overlay();
    return;
  }
  const MeshFaceRange& face = it->second.faces[static_cast<std::size_t>(row)];
  emit overlay_requested(face.bounds, static_cast<quint64>(item.node_id), isolate_->isChecked());
}

void RenderSceneInspector::emit_triangle_overlay() {
  if (!has_scene_ || draws_ == nullptr || draws_->currentRow() < 0 ||
      draws_->currentRow() >= static_cast<int>(scene_.items.size()) || tris_ == nullptr) {
    emit triangle_overlay_cleared();
    return;
  }
  const int row = tris_->currentRow();
  if (row < 0) {
    emit triangle_overlay_cleared();
    return;
  }
  const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(draws_->currentRow())];
  const auto it = scene_.meshes.find(item.mesh_asset_id);
  if (it == scene_.meshes.end() || it->second.line_list) {
    emit triangle_overlay_cleared();
    return;
  }
  const MeshCpu& mesh = it->second;
  const std::size_t idx = static_cast<std::size_t>(row) * 3;
  if (idx + 2 >= mesh.indices.size()) {
    emit triangle_overlay_cleared();
    return;
  }
  const std::uint32_t a = mesh.indices[idx + 0];
  const std::uint32_t b = mesh.indices[idx + 1];
  const std::uint32_t c = mesh.indices[idx + 2];
  if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || c >= mesh.vertices.size()) {
    emit triangle_overlay_cleared();
    return;
  }
  const Vec3 v0 = item.transform * mesh.vertices[a].position;
  const Vec3 v1 = item.transform * mesh.vertices[b].position;
  const Vec3 v2 = item.transform * mesh.vertices[c].position;
  emit triangle_overlay_requested(v0, v1, v2);
}

void RenderSceneInspector::set_texture_thumb(QLabel* image, QLabel* caption, std::uint64_t tex_id,
                                             const QString& kind) {
  if (tex_id == 0) {
    image->setPixmap(QPixmap());
    image->setText(tr("None"));
    caption->setText(kind);
    return;
  }
  const auto it = scene_.textures.find(tex_id);
  if (it == scene_.textures.end()) {
    image->setPixmap(QPixmap());
    image->setText(tr("Missing"));
    caption->setText(tr("%1  #%2").arg(kind).arg(tex_id));
    return;
  }
  const TextureAsset& tex = it->second;
  const int w = static_cast<int>(tex.width);
  const int h = static_cast<int>(tex.height);
  const qsizetype expected = static_cast<qsizetype>(w) * h * 4;
  caption->setText(tr("%1  #%2\n%3×%4  %5")
                       .arg(kind)
                       .arg(tex_id)
                       .arg(w)
                       .arg(h)
                       .arg(tex.srgb ? tr("sRGB") : tr("linear")));
  if (w <= 0 || h <= 0 || static_cast<qsizetype>(tex.rgba.size()) < expected) {
    image->setPixmap(QPixmap());
    image->setText(tr("No pixels"));
    return;
  }
  const QImage qimage(tex.rgba.data(), w, h, w * 4, QImage::Format_RGBA8888);
  image->setText(QString());
  image->setPixmap(QPixmap::fromImage(
      qimage.copy().scaled(image->size(), Qt::KeepAspectRatio, Qt::FastTransformation)));
}

void RenderSceneInspector::set_color_chip(QLabel* chip, const Vec3& color) {
  const QColor qc = vec_color(color);
  chip->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid #555;")
                          .arg(qc.name(QColor::HexRgb)));
}

void RenderSceneInspector::emit_overlay() {
  if (!has_scene_ || draws_->currentRow() < 0 ||
      draws_->currentRow() >= static_cast<int>(scene_.items.size())) {
    emit overlay_cleared();
    return;
  }
  const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(draws_->currentRow())];
  emit overlay_requested(item.bounds, static_cast<quint64>(item.node_id), isolate_->isChecked());
}

void RenderSceneInspector::emit_vertex_overlay() {
  if (!has_scene_ || draws_->currentRow() < 0 ||
      draws_->currentRow() >= static_cast<int>(scene_.items.size())) {
    emit vertex_overlay_cleared();
    return;
  }
  const int row = verts_->currentRow();
  if (row < 0) {
    emit vertex_overlay_cleared();
    return;
  }
  const SceneDrawItem& item = scene_.items[static_cast<std::size_t>(draws_->currentRow())];
  const auto it = scene_.meshes.find(item.mesh_asset_id);
  if (it == scene_.meshes.end()) {
    emit vertex_overlay_cleared();
    return;
  }
  const MeshCpu& mesh = it->second;
  if (row >= static_cast<int>(mesh.vertices.size())) {
    emit vertex_overlay_cleared();
    return;
  }
  const Vertex& v = mesh.vertices[static_cast<std::size_t>(row)];
  DebugVertexOverlay mark;
  mark.index = row;
  mark.world = item.transform * v.position;
  mark.normal = transform_normal_affine(item.transform, v.normal);
  mark.color = v.color;
  mark.uv = v.uv;
  emit vertex_overlay_requested(mark);
}

}  // namespace tamias
