#include "grid_settings_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace tamias {
namespace {

constexpr int kNameColumn = 0;
constexpr int kDirectionColumn = 1;
constexpr int kPositionColumn = 2;
constexpr int kStartColumn = 3;
constexpr int kEndColumn = 4;
constexpr int kColumnCount = 5;
constexpr double kDefaultLength = 20.0;

// 轴 id 存在名称单元格的 UserRole 上：整行可以删了再加，句柄不丢。
constexpr int kIdRole = Qt::UserRole;

QDoubleSpinBox* make_length_spin(QWidget* parent, double value) {
  auto* spin = new QDoubleSpinBox(parent);
  spin->setRange(-1.0e6, 1.0e6);
  spin->setDecimals(3);
  spin->setSingleStep(1.0);
  spin->setSuffix(QStringLiteral(" m"));
  spin->setKeyboardTracking(false);
  spin->setValue(value);
  return spin;
}

GridAxisDirection combo_direction(const QComboBox* combo) {
  return combo != nullptr && combo->currentData().toInt() ==
                                 static_cast<int>(GridAxisDirection::AlongX)
             ? GridAxisDirection::AlongX
             : GridAxisDirection::AlongZ;
}

// "6,6,6" / "6 6 6" / "6000，6000" 都吃。
std::vector<double> parse_spacings(const QString& text, bool* ok) {
  std::vector<double> values;
  *ok = true;
  const QStringList parts = text.split(QRegularExpression(QStringLiteral("[,\\s，、;]+")),
                                       Qt::SkipEmptyParts);
  for (const QString& part : parts) {
    bool good = false;
    const double value = part.toDouble(&good);
    if (!good || value <= 0.0) {
      *ok = false;
      return {};
    }
    values.push_back(value);
  }
  return values;
}

}  // namespace

// 放在类里而不是匿名命名空间：tr() 的上下文才是本对话框（否则归到 QObject 名下）。
QComboBox* GridSettingsDialog::make_direction_combo(GridAxisDirection direction) {
  auto* combo = new QComboBox(table_);
  combo->addItem(tr("Vertical (Z)"), static_cast<int>(GridAxisDirection::AlongZ));
  combo->addItem(tr("Horizontal (X)"), static_cast<int>(GridAxisDirection::AlongX));
  combo->setCurrentIndex(direction == GridAxisDirection::AlongX ? 1 : 0);
  return combo;
}

GridSettingsDialog::GridSettingsDialog(std::vector<GridAxis> axes, QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("gridSettingsDialog"));
  setWindowTitle(tr("Grid Settings"));
  setModal(true);
  resize(720, 460);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  table_ = new QTableWidget(this);
  table_->setColumnCount(kColumnCount);
  table_->setHorizontalHeaderLabels(
      {tr("Name"), tr("Direction"), tr("Position"), tr("Start"), tr("End")});
  table_->verticalHeader()->setVisible(false);
  table_->setSelectionBehavior(QAbstractItemView::SelectRows);
  table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  table_->horizontalHeader()->setSectionResizeMode(kNameColumn, QHeaderView::Stretch);
  for (int column = kDirectionColumn; column < kColumnCount; ++column) {
    table_->horizontalHeader()->setSectionResizeMode(column, QHeaderView::Fixed);
    table_->setColumnWidth(column, 130);
  }
  root->addWidget(table_, 1);

  auto* tools = new QHBoxLayout();
  tools->setSpacing(8);
  auto* add_button = new QPushButton(tr("Add Axis"), this);
  connect(add_button, &QPushButton::clicked, this, [this] { add_empty_axis(); });
  tools->addWidget(add_button);
  auto* remove_button = new QPushButton(tr("Remove Selected"), this);
  remove_button->setToolTip(tr("Delete the selected axes from the table"));
  connect(remove_button, &QPushButton::clicked, this, [this] { remove_selected(); });
  tools->addWidget(remove_button);
  tools->addStretch(1);
  root->addLayout(tools);

  auto* generate_box = new QHBoxLayout();
  generate_box->setSpacing(8);
  generate_box->addWidget(new QLabel(tr("Numbered spacing"), this));
  x_spacings_ = new QLineEdit(QStringLiteral("6,6,6"), this);
  x_spacings_->setToolTip(tr("Spacing between vertical (numbered) axes, in metres"));
  generate_box->addWidget(x_spacings_, 1);
  generate_box->addWidget(new QLabel(tr("Lettered spacing"), this));
  z_spacings_ = new QLineEdit(QStringLiteral("5,5"), this);
  z_spacings_->setToolTip(tr("Spacing between horizontal (lettered) axes, in metres"));
  generate_box->addWidget(z_spacings_, 1);
  generate_box->addWidget(new QLabel(tr("Origin"), this));
  origin_x_ = make_length_spin(this, 0.0);
  origin_z_ = make_length_spin(this, 0.0);
  generate_box->addWidget(origin_x_);
  generate_box->addWidget(origin_z_);
  generate_box->addWidget(new QLabel(tr("Margin"), this));
  margin_ = make_length_spin(this, 1.0);
  margin_->setSingleStep(0.5);
  generate_box->addWidget(margin_);
  auto* generate_button = new QPushButton(tr("Generate"), this);
  connect(generate_button, &QPushButton::clicked, this, [this] { generate_orthogonal(); });
  generate_box->addWidget(generate_button);
  root->addLayout(generate_box);

  auto* hint = new QLabel(
      tr("Generating replaces the table above. Positions are in metres, measured from the origin."),
      this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  reload(std::move(axes));
}

void GridSettingsDialog::reload(const std::vector<GridAxis>& axes) {
  table_->setRowCount(0);
  for (const GridAxis& axis : axes) {
    add_row(axis);
  }
}

void GridSettingsDialog::add_row(const GridAxis& axis) {
  const int row = table_->rowCount();
  table_->insertRow(row);

  auto* name = new QTableWidgetItem(QString::fromStdString(axis.name));
  name->setData(kIdRole, QVariant::fromValue<qulonglong>(axis.id));
  table_->setItem(row, kNameColumn, name);

  table_->setCellWidget(row, kDirectionColumn, make_direction_combo(axis.direction));
  const double start = axis.length() > 0.0 ? axis.start : -kDefaultLength * 0.5;
  const double end = axis.length() > 0.0 ? axis.end : kDefaultLength * 0.5;
  table_->setCellWidget(row, kPositionColumn, make_length_spin(table_, axis.position));
  table_->setCellWidget(row, kStartColumn, make_length_spin(table_, start));
  table_->setCellWidget(row, kEndColumn, make_length_spin(table_, end));
}

void GridSettingsDialog::add_empty_axis() {
  // 新轴默认给一个没被占用的编号名（A/B/C 留给字母轴，新行按数字往下排）。
  int serial = table_->rowCount() + 1;
  auto name_taken = [this](const QString& candidate) {
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem* item = table_->item(row, kNameColumn);
      if (item != nullptr && item->text() == candidate) {
        return true;
      }
    }
    return false;
  };
  while (name_taken(QString::number(serial))) {
    ++serial;
  }

  GridAxis axis;
  axis.id = 0;  // 新增：保存时由 UpdateGridCommand 分配句柄
  axis.name = std::to_string(serial);
  axis.direction = GridAxisDirection::AlongZ;
  axis.position = 0.0;
  add_row(axis);
  table_->selectRow(table_->rowCount() - 1);
}

void GridSettingsDialog::remove_selected() {
  std::vector<int> rows;
  for (const QModelIndex& index : table_->selectionModel()->selectedRows()) {
    rows.push_back(index.row());
  }
  std::sort(rows.rbegin(), rows.rend());
  for (const int row : rows) {
    table_->removeRow(row);
  }
}

void GridSettingsDialog::generate_orthogonal() {
  bool ok_x = true;
  bool ok_z = true;
  const std::vector<double> xs = parse_spacings(x_spacings_->text(), &ok_x);
  const std::vector<double> zs = parse_spacings(z_spacings_->text(), &ok_z);
  if (!ok_x || !ok_z || (xs.empty() && zs.empty())) {
    QMessageBox::warning(this, tr("Grid Settings"),
                         tr("Spacing must be a list of positive numbers, e.g. 6,6,6."));
    return;
  }
  reload(make_orthogonal_grid(origin_x_->value(), origin_z_->value(), xs, zs, margin_->value()));
}

std::vector<GridAxis> GridSettingsDialog::axes() const {
  std::vector<GridAxis> out;
  out.reserve(static_cast<std::size_t>(table_->rowCount()));
  for (int row = 0; row < table_->rowCount(); ++row) {
    const QTableWidgetItem* name = table_->item(row, kNameColumn);
    const auto* direction =
        qobject_cast<const QComboBox*>(table_->cellWidget(row, kDirectionColumn));
    const auto* position =
        qobject_cast<const QDoubleSpinBox*>(table_->cellWidget(row, kPositionColumn));
    const auto* start = qobject_cast<const QDoubleSpinBox*>(table_->cellWidget(row, kStartColumn));
    const auto* end = qobject_cast<const QDoubleSpinBox*>(table_->cellWidget(row, kEndColumn));
    if (position == nullptr || start == nullptr || end == nullptr) {
      continue;
    }
    GridAxis axis;
    axis.id = name != nullptr ? name->data(kIdRole).toULongLong() : 0;
    axis.name = name != nullptr ? name->text().toStdString() : std::string();
    axis.direction = combo_direction(direction);
    axis.position = position->value();
    axis.start = start->value();
    axis.end = end->value();
    if (axis.end < axis.start) {
      std::swap(axis.start, axis.end);
    }
    if (axis.name.empty()) {
      axis.name = std::to_string(row + 1);
    }
    out.push_back(std::move(axis));
  }
  return out;
}

}  // namespace tamias
