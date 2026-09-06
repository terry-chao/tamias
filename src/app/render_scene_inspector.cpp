#include "render_scene_inspector.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColor>
#include <QFontDatabase>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
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
  setMinimumWidth(360);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(8, 8, 8, 8);
  root->setSpacing(8);

  empty_ = new QLabel(tr("Open a .trscn, or capture the current view."), this);
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
  summary_layout->addWidget(summary_mode_);
  summary_layout->addWidget(summary_counts_);
  summary_layout->addWidget(summary_digest_);
  summary_layout->addWidget(summary_camera_);

  auto* actions = new QHBoxLayout();
  auto* refresh_btn = new QPushButton(tr("Refresh"), summary);
  auto* dump_btn = new QPushButton(tr("Write debug files…"), summary);
  dump_btn->setToolTip(tr("Dump inspect.txt, OBJ meshes, and PPM textures next to the scene"));
  connect(refresh_btn, &QPushButton::clicked, this, &RenderSceneInspector::refresh_requested);
  connect(dump_btn, &QPushButton::clicked, this, &RenderSceneInspector::dump_requested);
  actions->addStretch();
  actions->addWidget(refresh_btn);
  actions->addWidget(dump_btn);
  summary_layout->addLayout(actions);
  body_layout->addWidget(summary);

  auto* split = new QSplitter(Qt::Vertical, body_);
  split->setChildrenCollapsible(false);

  auto* list_box = new QGroupBox(tr("Draws"), split);
  auto* list_layout = new QVBoxLayout(list_box);
  list_layout->setContentsMargins(8, 8, 8, 8);
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
  field_transform_ = make_value(detail_box);
  field_transform_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
  form->addRow(tr("Node"), field_node_);
  form->addRow(tr("Mesh"), field_mesh_);
  form->addRow(tr("Flags"), field_flags_);
  form->addRow(tr("AABB"), field_aabb_);
  form->addRow(tr("Translation"), field_translation_);
  form->addRow(tr("PBR"), field_pbr_);
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
  rebuild();
}

void RenderSceneInspector::clear() {
  scene_ = {};
  has_scene_ = false;
  rebuild();
  emit overlay_cleared();
  emit vertex_overlay_cleared();
}

void RenderSceneInspector::show_empty() {
  empty_->show();
  body_->hide();
  draws_->setRowCount(0);
  verts_->setRowCount(0);
  dump_->clear();
  set_vertices_tab_title(-1);
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
  summary_counts_->setText(tr("%1 draws · %2 meshes · %3 textures · %4 tris")
                               .arg(scene_.items.size())
                               .arg(scene_.meshes.size())
                               .arg(scene_.textures.size())
                               .arg(render_scene_triangle_count(scene_)));
  summary_digest_->setText(tr("digest  %1").arg(digest));
  summary_camera_->setText(tr("eye %1\ntarget %2\ndistance %3")
                               .arg(fmt_vec(scene_.view.eye_position), fmt_vec(scene_.view.target))
                               .arg(scene_.view.view_distance, 0, 'g', 4));
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
  if (select >= 0) {
    fill_selected_draw(select);
    emit_overlay();
  } else {
    fill_selected_draw(-1);
    emit overlay_cleared();
  }
  emit vertex_overlay_cleared();
}

void RenderSceneInspector::on_draw_selected() {
  if (!has_scene_ || draws_->currentRow() < 0) {
    fill_selected_draw(-1);
    emit overlay_cleared();
    emit vertex_overlay_cleared();
    return;
  }
  fill_selected_draw(draws_->currentRow());
  emit_overlay();
  emit vertex_overlay_cleared();
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
    field_transform_->setText(QStringLiteral("—"));
    set_color_chip(color_chip_, {0.5f, 0.5f, 0.5f});
    set_color_chip(category_chip_, {0.5f, 0.5f, 0.5f});
    set_texture_thumb(albedo_image_, albedo_caption_, 0, tr("Albedo"));
    set_texture_thumb(normal_image_, normal_caption_, 0, tr("Normal"));
    verts_->setRowCount(0);
    set_vertices_tab_title(-1);
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
  QString xf;
  for (int r = 0; r < 4; ++r) {
    xf += QStringLiteral("%1  %2  %3  %4\n")
              .arg(item.transform(r, 0), 8, 'g', 4)
              .arg(item.transform(r, 1), 8, 'g', 4)
              .arg(item.transform(r, 2), 8, 'g', 4)
              .arg(item.transform(r, 3), 8, 'g', 4);
  }
  field_transform_->setText(xf.trimmed());
  set_color_chip(color_chip_, item.color);
  set_color_chip(category_chip_, item.category_color);
  color_chip_->setToolTip(fmt_vec(item.color));
  category_chip_->setToolTip(fmt_vec(item.category_color));
  set_texture_thumb(albedo_image_, albedo_caption_, item.albedo_texture_id, tr("Albedo"));
  set_texture_thumb(normal_image_, normal_caption_, item.normal_texture_id, tr("Normal"));
  fill_vertices(item.mesh_asset_id);
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
