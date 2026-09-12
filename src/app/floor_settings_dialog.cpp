#include "floor_settings_dialog.h"

#include "bim/wall_size.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace tamias {
namespace {

constexpr int kNameColumn = 0;
constexpr int kElevationColumn = 1;
constexpr int kHeightColumn = 2;
constexpr int kTypeColumn = 3;
constexpr double kDefaultMezzanineHeight = 2.2;
constexpr double kTolerance = 1e-6;

QDoubleSpinBox* make_length_spin(QWidget* parent, double value) {
  auto* spin = new QDoubleSpinBox(parent);
  spin->setRange(-1.0e6, 1.0e6);
  spin->setDecimals(3);
  spin->setSingleStep(0.1);
  spin->setSuffix(QStringLiteral(" m"));
  spin->setKeyboardTracking(false);
  spin->setValue(value);
  return spin;
}

}  // namespace

FloorSettingsDialog::FloorSettingsDialog(std::vector<Storey> storeys,
                                         std::uint64_t active_storey_id, QWidget* parent)
    : QDialog(parent), storeys_(std::move(storeys)), active_storey_id_(active_storey_id) {
  setObjectName(QStringLiteral("floorSettingsDialog"));
  setWindowTitle(tr("Floor Settings"));
  setModal(true);
  resize(560, 420);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  table_ = new QTableWidget(this);
  table_->setColumnCount(4);
  table_->setHorizontalHeaderLabels({tr("Floor"), tr("Elevation"), tr("Floor Height"),
                                     tr("Type")});
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::SingleSelection);
  table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                          QAbstractItemView::SelectedClicked |
                          QAbstractItemView::EditKeyPressed);
  table_->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
  table_->horizontalHeader()->setSectionResizeMode(kElevationColumn, QHeaderView::Fixed);
  table_->horizontalHeader()->setSectionResizeMode(kHeightColumn, QHeaderView::Fixed);
  table_->horizontalHeader()->setSectionResizeMode(kTypeColumn, QHeaderView::Fixed);
  table_->setColumnWidth(kElevationColumn, 110);
  table_->setColumnWidth(kHeightColumn, 110);
  table_->setColumnWidth(kTypeColumn, 90);
  root->addWidget(table_, 1);

  auto* tools = new QHBoxLayout();
  tools->setSpacing(8);
  add_storey_ = new QPushButton(tr("Add Floor"), this);
  add_storey_->setToolTip(tr("Add a full storey above the topmost floor"));
  add_mezzanine_ = new QPushButton(tr("Add Mezzanine"), this);
  add_mezzanine_->setToolTip(
      tr("Insert a mezzanine between the selected floor and the one above it"));
  remove_ = new QPushButton(tr("Remove"), this);
  remove_->setToolTip(tr("Remove the selected floor (its components stay where they are)"));
  tools->addWidget(add_storey_);
  tools->addWidget(add_mezzanine_);
  tools->addWidget(remove_);
  tools->addStretch(1);
  root->addLayout(tools);

  hint_ = new QLabel(
      tr("Elevation and floor height are in metres. Changing a floor height moves the "
         "floors stacked above it; a mezzanine is a shorter floor inserted between two "
         "floors, and lifts the floors above it."),
      this);
  hint_->setWordWrap(true);
  hint_->setObjectName(QStringLiteral("floorSettingsHint"));
  hint_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  root->addWidget(hint_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &FloorSettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &FloorSettingsDialog::reject);
  connect(add_storey_, &QPushButton::clicked, this, &FloorSettingsDialog::add_storey_above);
  connect(add_mezzanine_, &QPushButton::clicked, this, &FloorSettingsDialog::add_mezzanine_above);
  connect(remove_, &QPushButton::clicked, this, &FloorSettingsDialog::remove_selected_row);

  std::sort(storeys_.begin(), storeys_.end(),
            [](const Storey& a, const Storey& b) { return a.elevation < b.elevation; });
  for (const Storey& storey : storeys_) {
    add_row(storey);
  }
  next_new_index_ = static_cast<int>(storeys_.size()) + 1;
  if (table_->rowCount() > 0) {
    int active_row = 0;
    for (int row = 0; row < table_->rowCount(); ++row) {
      if (table_->item(row, kNameColumn)->data(Qt::UserRole).toULongLong() ==
          active_storey_id_) {
        active_row = row;
        break;
      }
    }
    table_->selectRow(active_row);
  }
}

void FloorSettingsDialog::add_row(const Storey& storey, int at) {
  const int row =
      at < 0 || at > table_->rowCount() ? table_->rowCount() : at;
  table_->insertRow(row);

  auto* name_item = new QTableWidgetItem(QString::fromStdString(storey.name));
  name_item->setData(Qt::UserRole, static_cast<qulonglong>(storey.id));
  table_->setItem(row, kNameColumn, name_item);

  QDoubleSpinBox* elevation = make_length_spin(table_, storey.elevation);
  QDoubleSpinBox* height =
      make_length_spin(table_, storey.height > 0.0 ? storey.height : kDefaultWallHeight);
  // 改层高要知道"上一版层高"，控件自己留着最省事（valueChanged 里拿不到旧值）。
  height->setProperty("previousHeight", height->value());
  auto* type = new QComboBox(table_);
  type->addItem(tr("Floor"), 0);
  type->addItem(tr("Mezzanine"), 1);
  type->setCurrentIndex(storey.mezzanine ? 1 : 0);
  table_->setCellWidget(row, kElevationColumn, elevation);
  table_->setCellWidget(row, kHeightColumn, height);
  table_->setCellWidget(row, kTypeColumn, type);

  // 行号会在插行时变，所以信号里按控件现查行；旧层高由控件自己留着。
  connect(elevation, &QDoubleSpinBox::valueChanged, this, [this, height](double value) {
    height->setToolTip(tr("Top of this floor: %1 m").arg(value + height->value(), 0, 'f', 3));
  });
  connect(height, &QDoubleSpinBox::valueChanged, this,
          [this, elevation, height](double value) {
            const int edited = row_of_widget(height);
            if (edited >= 0) {
              restack_above(edited, elevation->value(),
                            height->property("previousHeight").toDouble());
            }
            height->setProperty("previousHeight", value);
            height->setToolTip(
                tr("Top of this floor: %1 m").arg(elevation->value() + value, 0, 'f', 3));
          });
  elevation->setToolTip(tr("Floor elevation"));
  height->setToolTip(tr("Top of this floor: %1 m")
                         .arg(elevation->value() + height->value(), 0, 'f', 3));
}

int FloorSettingsDialog::row_of_widget(const QWidget* widget) const {
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (table_->cellWidget(row, kElevationColumn) == widget ||
        table_->cellWidget(row, kHeightColumn) == widget ||
        table_->cellWidget(row, kTypeColumn) == widget) {
      return row;
    }
  }
  return -1;
}

double FloorSettingsDialog::elevation_at(int row) const {
  const auto* spin = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kElevationColumn));
  return spin != nullptr ? spin->value() : 0.0;
}

double FloorSettingsDialog::height_at(int row) const {
  const auto* spin = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kHeightColumn));
  return spin != nullptr ? spin->value() : kDefaultWallHeight;
}

bool FloorSettingsDialog::mezzanine_at(int row) const {
  const auto* combo = qobject_cast<QComboBox*>(table_->cellWidget(row, kTypeColumn));
  return combo != nullptr && combo->currentData().toInt() == 1;
}

void FloorSettingsDialog::set_elevation(int row, double value) {
  if (auto* spin = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kElevationColumn))) {
    spin->setValue(value);
  }
}

QString FloorSettingsDialog::name_at(int row) const {
  const QTableWidgetItem* item = table_->item(row, kNameColumn);
  return item != nullptr ? item->text() : QString();
}

void FloorSettingsDialog::restack_above(int edited_row, double old_elevation, double old_height) {
  const double edited_elevation = elevation_at(edited_row);
  double old_cursor = old_elevation + old_height;
  double new_cursor = edited_elevation + height_at(edited_row);
  if (std::abs(old_cursor - new_cursor) < kTolerance) {
    return;
  }
  std::vector<int> above;
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (row != edited_row && elevation_at(row) > edited_elevation + kTolerance) {
      above.push_back(row);
    }
  }
  std::sort(above.begin(), above.end(),
            [this](int a, int b) { return elevation_at(a) < elevation_at(b); });
  for (const int row : above) {
    // 只带动"正好压在旧顶标高上"的那一串；手工错开的楼层不动。
    if (std::abs(elevation_at(row) - old_cursor) > kTolerance) {
      break;
    }
    const double height = height_at(row);
    set_elevation(row, new_cursor);
    old_cursor += height;
    new_cursor += height;
  }
}

void FloorSettingsDialog::lift_above(int row, double delta) {
  if (std::abs(delta) < kTolerance) {
    return;
  }
  const double base = elevation_at(row);
  for (int other = 0; other < table_->rowCount(); ++other) {
    if (other != row && elevation_at(other) > base + kTolerance) {
      set_elevation(other, elevation_at(other) + delta);
    }
  }
}

void FloorSettingsDialog::add_storey_above() {
  Storey storey;
  double top = 0.0;
  bool any = false;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const double row_top = elevation_at(row) + height_at(row);
    if (!any || row_top > top) {
      top = row_top;
      any = true;
    }
  }
  storey.elevation = any ? top : 0.0;
  storey.height = kDefaultWallHeight;
  storey.name = tr("Floor %1").arg(next_new_index_).toStdString();
  ++next_new_index_;
  add_row(storey);
  table_->selectRow(table_->rowCount() - 1);
}

void FloorSettingsDialog::add_mezzanine_above() {
  int row = table_->currentRow();
  if (row < 0) {
    row = table_->rowCount() - 1;
  }
  if (row < 0) {
    // 空表：夹层就是第一层。
    add_storey_above();
    return;
  }
  const double elevation = elevation_at(row) + height_at(row);
  // 先给夹层腾地方：上面的楼层整体抬高一个夹层层高，层与层之间不留缝。
  lift_above(row, kDefaultMezzanineHeight);

  Storey storey;
  storey.mezzanine = true;
  storey.height = kDefaultMezzanineHeight;
  storey.elevation = elevation;
  storey.name = tr("Mezzanine %1").arg(next_new_index_).toStdString();
  ++next_new_index_;
  add_row(storey, row + 1);
  table_->selectRow(row + 1);
}

void FloorSettingsDialog::remove_selected_row() {
  const int row = table_->currentRow();
  if (row < 0) {
    return;
  }
  table_->removeRow(row);
}

bool FloorSettingsDialog::collect(QString* error) {
  std::vector<Storey> plan;
  plan.reserve(static_cast<std::size_t>(table_->rowCount()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    Storey storey;
    storey.id = table_->item(row, kNameColumn)->data(Qt::UserRole).toULongLong();
    storey.name = name_at(row).trimmed().toStdString();
    storey.elevation = elevation_at(row);
    storey.height = height_at(row);
    storey.mezzanine = mezzanine_at(row);
    if (storey.name.empty()) {
      *error = tr("Every floor needs a name.");
      return false;
    }
    plan.push_back(std::move(storey));
  }

  for (std::size_t i = 0; i < plan.size(); ++i) {
    for (std::size_t j = i + 1; j < plan.size(); ++j) {
      if (plan[i].name == plan[j].name) {
        *error = tr("Floor names must be unique: %1")
                     .arg(QString::fromStdString(plan[i].name));
        return false;
      }
      if (std::abs(plan[i].elevation - plan[j].elevation) < 1e-4) {
        *error = tr("Two floors cannot share the same elevation: %1 m")
                     .arg(plan[i].elevation, 0, 'f', 3);
        return false;
      }
    }
  }

  std::sort(plan.begin(), plan.end(),
            [](const Storey& a, const Storey& b) { return a.elevation < b.elevation; });
  storeys_ = std::move(plan);
  return true;
}

void FloorSettingsDialog::accept() {
  QString error;
  if (!collect(&error)) {
    QMessageBox::warning(this, tr("Floor Settings"), error);
    return;
  }
  QDialog::accept();
}

}  // namespace tamias
