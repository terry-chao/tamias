#include "drawing_import_dialog.h"

#include "engine/core/log.h"
#include "engine/drawing/dxf_reader.h"
#include "qt_path.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <cstddef>
#include <string>
#include <utility>

namespace tamias {
namespace {

// 候选表里的行类型与下标：勾选状态就是第 0 列的复选框。
enum class RowKind { Wall = 0, Column = 1, Opening = 2 };
constexpr int kKindRole = Qt::UserRole;
constexpr int kIndexRole = Qt::UserRole + 1;

QDoubleSpinBox* make_spin(QWidget* parent, double value, double min, double max,
                          double step = 0.05) {
  auto* spin = new QDoubleSpinBox(parent);
  spin->setRange(min, max);
  spin->setDecimals(3);
  spin->setSingleStep(step);
  spin->setSuffix(QStringLiteral(" m"));
  spin->setKeyboardTracking(false);
  spin->setValue(value);
  return spin;
}

QString trim_number(double value) {
  return QString::number(value, 'f', 3);
}

}  // namespace

DrawingImportDialog::DrawingImportDialog(const QString& drawing_path, const Grid* grid,
                                         QWidget* parent)
    : QDialog(parent), path_(drawing_path), grid_(grid) {
  setObjectName(QStringLiteral("drawingImportDialog"));
  setWindowTitle(tr("Trace Drawing to BIM"));
  setModal(true);
  resize(940, 660);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  // ---- 图纸与参数 ----
  auto* source_box = new QGroupBox(tr("Source"), this);
  auto* source_grid = new QFormLayout(source_box);
  auto* path_row = new QHBoxLayout();
  path_edit_ = new QLineEdit(drawing_path, source_box);
  path_edit_->setPlaceholderText(tr("A DXF drawing exported from your CAD file"));
  path_edit_->setReadOnly(true);
  path_row->addWidget(path_edit_, 1);
  auto* browse_button = new QPushButton(tr("Browse…"), source_box);
  connect(browse_button, &QPushButton::clicked, this, [this] { browse(); });
  path_row->addWidget(browse_button);
  source_grid->addRow(tr("Drawing"), path_row);

  unit_combo_ = new QComboBox(source_box);
  unit_combo_->addItem(tr("From drawing"), 0.0);
  unit_combo_->addItem(tr("Millimetres"), 0.001);
  unit_combo_->addItem(tr("Centimetres"), 0.01);
  unit_combo_->addItem(tr("Metres"), 1.0);
  unit_combo_->addItem(tr("Inches"), 0.0254);
  unit_combo_->addItem(tr("Feet"), 0.3048);
  unit_combo_->setToolTip(
      tr("Drawing units to metres. Prefer the value stored in the file; override it here when "
         "the drawing has none."));
  source_grid->addRow(tr("Units"), unit_combo_);
  root->addWidget(source_box);

  auto* layers_box = new QGroupBox(tr("Layers"), this);
  auto* layers_grid = new QFormLayout(layers_box);
  wall_layers_ = new QLineEdit(QString::fromStdString(DrawingImportOptions{}.wall_layers),
                               layers_box);
  column_layers_ =
      new QLineEdit(QString::fromStdString(DrawingImportOptions{}.column_layers), layers_box);
  opening_layers_ =
      new QLineEdit(QString::fromStdString(DrawingImportOptions{}.opening_layers), layers_box);
  const QString layer_hint = tr("Comma separated, case-insensitive, matched as substring");
  wall_layers_->setToolTip(layer_hint);
  column_layers_->setToolTip(layer_hint);
  opening_layers_->setToolTip(layer_hint);
  layers_grid->addRow(tr("Walls"), wall_layers_);
  layers_grid->addRow(tr("Columns"), column_layers_);
  layers_grid->addRow(tr("Doors / windows"), opening_layers_);
  root->addWidget(layers_box);

  auto* sizes_box = new QGroupBox(tr("Sizes"), this);
  auto* sizes_grid = new QHBoxLayout(sizes_box);
  wall_thickness_ = make_spin(sizes_box, 0.2, 0.05, 2.0);
  wall_height_ = make_spin(sizes_box, 3.0, 0.5, 20.0, 0.1);
  column_height_ = make_spin(sizes_box, 3.0, 0.5, 20.0, 0.1);
  host_tolerance_ = make_spin(sizes_box, 0.35, 0.05, 2.0);
  sizes_grid->addWidget(new QLabel(tr("Wall thickness"), sizes_box));
  sizes_grid->addWidget(wall_thickness_);
  sizes_grid->addWidget(new QLabel(tr("Wall height"), sizes_box));
  sizes_grid->addWidget(wall_height_);
  sizes_grid->addWidget(new QLabel(tr("Column height"), sizes_box));
  sizes_grid->addWidget(column_height_);
  sizes_grid->addWidget(new QLabel(tr("Host tolerance"), sizes_box));
  sizes_grid->addWidget(host_tolerance_);
  snap_to_grid_ = new QCheckBox(tr("Snap wall ends to grid"), sizes_box);
  snap_to_grid_->setChecked(true);
  sizes_grid->addWidget(snap_to_grid_);
  root->addWidget(sizes_box);

  auto* recognize_row = new QHBoxLayout();
  auto* recognize_button = new QPushButton(tr("Recognise"), this);
  connect(recognize_button, &QPushButton::clicked, this, [this] { recognize(); });
  recognize_row->addWidget(recognize_button);
  summary_ = new QLabel(tr("Pick a drawing, then press Recognise."), this);
  recognize_row->addWidget(summary_, 1);
  auto* all_button = new QPushButton(tr("Select All"), this);
  connect(all_button, &QPushButton::clicked, this, [this] { set_all_checked(true); });
  recognize_row->addWidget(all_button);
  auto* none_button = new QPushButton(tr("Select None"), this);
  connect(none_button, &QPushButton::clicked, this, [this] { set_all_checked(false); });
  recognize_row->addWidget(none_button);
  auto* confident_button = new QPushButton(tr("High Confidence"), this);
  connect(confident_button, &QPushButton::clicked, this, [this] { keep_high_confidence(); });
  recognize_row->addWidget(confident_button);
  root->addLayout(recognize_row);

  table_ = new QTableWidget(this);
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels({tr("Component"), tr("Source"), tr("Parameters"), tr("At")});
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Fixed);
  table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
  table_->setColumnWidth(0, 130);
  connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
    update_summary();
  });
  root->addWidget(table_, 1);

  warnings_ = new QPlainTextEdit(this);
  warnings_->setReadOnly(true);
  warnings_->setMaximumHeight(84);
  warnings_->setPlaceholderText(tr("Notes from the recogniser"));
  root->addWidget(warnings_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_button_ = buttons->button(QDialogButtonBox::Ok);
  ok_button_->setText(tr("Create Model"));
  ok_button_->setEnabled(false);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  if (!path_.isEmpty()) {
    recognize();
  }
}

void DrawingImportDialog::browse() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Open Drawing"), QString(),
                                                    tr("DXF drawing (*.dxf)"));
  if (path.isEmpty()) {
    return;
  }
  path_ = path;
  path_edit_->setText(path);
  recognize();
}

void DrawingImportDialog::accept() {
  std::vector<bool> keep_walls(plan_.walls.size(), false);
  std::vector<bool> keep_columns(plan_.columns.size(), false);
  std::vector<bool> keep_openings(plan_.openings.size(), false);
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QTableWidgetItem* item = table_->item(row, 0);
    if (item == nullptr || item->checkState() != Qt::Checked) {
      continue;
    }
    const auto index = static_cast<std::size_t>(item->data(kIndexRole).toULongLong());
    switch (static_cast<RowKind>(item->data(kKindRole).toInt())) {
      case RowKind::Wall:
        if (index < keep_walls.size()) {
          keep_walls[index] = true;
        }
        break;
      case RowKind::Column:
        if (index < keep_columns.size()) {
          keep_columns[index] = true;
        }
        break;
      case RowKind::Opening:
        if (index < keep_openings.size()) {
          keep_openings[index] = true;
        }
        break;
    }
  }
  filtered_ = filter_drawing_import_plan(plan_, keep_walls, keep_columns, keep_openings,
                                         &dropped_openings_);
  QDialog::accept();
}

DrawingImportOptions DrawingImportDialog::options() const {
  DrawingImportOptions options;
  options.wall_layers = wall_layers_->text().toStdString();
  options.column_layers = column_layers_->text().toStdString();
  options.opening_layers = opening_layers_->text().toStdString();
  options.wall_thickness = wall_thickness_->value();
  options.wall_height = wall_height_->value();
  options.column_height = column_height_->value();
  options.host_tolerance = host_tolerance_->value();
  options.align_to_grid = snap_to_grid_->isChecked();
  options.unit_scale = unit_combo_->currentData().toDouble();
  return options;
}

void DrawingImportDialog::recognize() {
  if (path_.isEmpty()) {
    QMessageBox::information(this, tr("Trace Drawing to BIM"), tr("Pick a DXF drawing first."));
    return;
  }
  if (!loaded_) {
    auto loaded = load_dxf(qstring_to_path(path_));
    if (!loaded) {
      QMessageBox::warning(this, tr("Trace Drawing to BIM"),
                           tr("Cannot read this drawing: %1")
                               .arg(QString::fromStdString(loaded.error())));
      return;
    }
    drawing_ = std::move(*loaded);
    loaded_ = true;
  }
  plan_ = build_drawing_import_plan(drawing_, options(), grid_);
  rebuild_table();
}

void DrawingImportDialog::rebuild_table() {
  QSignalBlocker block(table_);
  table_->setRowCount(0);

  const auto add_row = [&](RowKind kind, std::size_t index, const QString& name,
                           const QString& source, const QString& params, const QString& at,
                           bool checked) {
    const int row = table_->rowCount();
    table_->insertRow(row);
    auto* item = new QTableWidgetItem(name);
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    item->setData(kKindRole, static_cast<int>(kind));
    item->setData(kIndexRole, static_cast<qulonglong>(index));
    table_->setItem(row, 0, item);
    table_->setItem(row, 1, new QTableWidgetItem(source));
    table_->setItem(row, 2, new QTableWidgetItem(params));
    table_->setItem(row, 3, new QTableWidgetItem(at));
  };

  for (std::size_t i = 0; i < plan_.walls.size(); ++i) {
    const WallCandidate& wall = plan_.walls[i];
    add_row(RowKind::Wall, i, tr("Wall"), QString::fromStdString(wall.layer),
            tr("thickness %1 m, height %2 m")
                .arg(trim_number(wall.thickness), trim_number(wall.height)),
            tr("(%1, %2) → (%3, %4)")
                .arg(trim_number(wall.start.x), trim_number(wall.start.z),
                     trim_number(wall.end.x), trim_number(wall.end.z)),
            true);
  }
  for (std::size_t i = 0; i < plan_.columns.size(); ++i) {
    const ColumnCandidate& column = plan_.columns[i];
    add_row(RowKind::Column, i, tr("Column"), QString::fromStdString(column.layer),
            column.circular ? tr("round, %1 m").arg(trim_number(column.width))
                            : tr("%1 × %2 m")
                                  .arg(trim_number(column.width), trim_number(column.depth)),
            tr("(%1, %2)")
                .arg(trim_number(column.position.x), trim_number(column.position.z)),
            column.confidence >= 0.9f);
  }
  for (std::size_t i = 0; i < plan_.openings.size(); ++i) {
    const OpeningCandidate& opening = plan_.openings[i];
    const QString source = opening.block.empty() ? QString::fromStdString(opening.layer)
                                                 : QString::fromStdString(opening.block);
    add_row(RowKind::Opening, i, opening.door ? tr("Door") : tr("Window"), source,
            tr("%1 × %2 m").arg(trim_number(opening.width), trim_number(opening.height)),
            tr("(%1, %2)")
                .arg(trim_number(opening.position.x), trim_number(opening.position.z)),
            true);
  }

  QStringList notes;
  for (const std::string& warning : plan_.warnings) {
    notes << QString::fromStdString(warning);
  }
  if (plan_.skipped_segments > 0) {
    notes << tr("Skipped %1 segments shorter than the minimum wall length.")
                 .arg(plan_.skipped_segments);
  }
  warnings_->setPlainText(notes.join(QStringLiteral("\n")));
  update_summary();
}

void DrawingImportDialog::set_all_checked(bool checked) {
  QSignalBlocker block(table_);
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (QTableWidgetItem* item = table_->item(row, 0)) {
      item->setCheckState(checked ? Qt::Checked : Qt::Unchecked);
    }
  }
  update_summary();
}

void DrawingImportDialog::keep_high_confidence() {
  QSignalBlocker block(table_);
  for (int row = 0; row < table_->rowCount(); ++row) {
    QTableWidgetItem* item = table_->item(row, 0);
    if (item == nullptr) {
      continue;
    }
    const auto kind = static_cast<RowKind>(item->data(kKindRole).toInt());
    const auto index = static_cast<std::size_t>(item->data(kIndexRole).toULongLong());
    float confidence = 1.0f;
    if (kind == RowKind::Wall && index < plan_.walls.size()) {
      confidence = plan_.walls[index].confidence;
    } else if (kind == RowKind::Column && index < plan_.columns.size()) {
      confidence = plan_.columns[index].confidence;
    } else if (kind == RowKind::Opening && index < plan_.openings.size()) {
      confidence = plan_.openings[index].confidence;
    }
    item->setCheckState(confidence >= 0.9f ? Qt::Checked : Qt::Unchecked);
  }
  update_summary();
}

std::size_t DrawingImportDialog::checked_count() const {
  std::size_t count = 0;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QTableWidgetItem* item = table_->item(row, 0);
    if (item != nullptr && item->checkState() == Qt::Checked) {
      ++count;
    }
  }
  return count;
}

void DrawingImportDialog::update_summary() {
  const std::size_t checked = checked_count();
  std::size_t walls = 0;
  std::size_t columns = 0;
  std::size_t openings = 0;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QTableWidgetItem* item = table_->item(row, 0);
    if (item == nullptr || item->checkState() != Qt::Checked) {
      continue;
    }
    switch (static_cast<RowKind>(item->data(kKindRole).toInt())) {
      case RowKind::Wall:
        ++walls;
        break;
      case RowKind::Column:
        ++columns;
        break;
      case RowKind::Opening:
        ++openings;
        break;
    }
  }
  summary_->setText(tr("Selected %1 of %2 — walls %3, columns %4, doors/windows %5")
                        .arg(checked)
                        .arg(plan_.size())
                        .arg(walls)
                        .arg(columns)
                        .arg(openings));
  if (ok_button_ != nullptr) {
    ok_button_->setEnabled(checked > 0);
  }
}

}  // namespace tamias
