#include "app/bim/grid/grid_settings_dialog.h"

#include <QCheckBox>
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
// 新增一根轴时离同方向最后一根轴多远（米）：沿用「生成」那一行的默认间距，
// 纵轴（编号轴，x 方向）6、横轴（字母轴，z 方向）5。不然新轴全落在 0，几根叠成一根。
constexpr double kDefaultVerticalSpacing = 6.0;
constexpr double kDefaultHorizontalSpacing = 5.0;

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
  // 逐根加轴：竖轴（编号轴 1/2/3…，沿 Z）和横轴（字母轴 A/B/C…，沿 X）分开两颗按钮，
  // 免得只有一根默认方向、想加另一种还得手动改方向列。
  auto* add_vertical_button = new QPushButton(tr("Add Vertical Axis"), this);
  add_vertical_button->setToolTip(
      tr("Add a numbered axis running along Z (1, 2, 3 …)"));
  connect(add_vertical_button, &QPushButton::clicked, this,
          [this] { add_empty_axis(GridAxisDirection::AlongZ); });
  tools->addWidget(add_vertical_button);
  auto* add_horizontal_button = new QPushButton(tr("Add Horizontal Axis"), this);
  add_horizontal_button->setToolTip(
      tr("Add a lettered axis running along X (A, B, C …)"));
  connect(add_horizontal_button, &QPushButton::clicked, this,
          [this] { add_empty_axis(GridAxisDirection::AlongX); });
  tools->addWidget(add_horizontal_button);
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
  z_spacings_ = new QLineEdit(QStringLiteral("6,6,6"), this);
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
  connect(margin_, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
          [this](double) { maybe_auto_fit(); });
  generate_box->addWidget(margin_);
  auto* generate_button = new QPushButton(tr("Generate"), this);
  connect(generate_button, &QPushButton::clicked, this, [this] { generate_orthogonal(); });
  generate_box->addWidget(generate_button);
  root->addLayout(generate_box);

  // 轴网本来就该是「一张网」：每根轴跨到整张网的两端，任意竖轴 × 横轴都真的相交。
  // 勾着（默认）时，加轴 / 改位置 / 改外扩都自动把端点拉齐；取消勾选才回到逐根手填端点。
  auto* fit_box = new QHBoxLayout();
  fit_box->setSpacing(8);
  auto_fit_ = new QCheckBox(tr("Auto-fit axis lengths so the grid stays a mesh"), this);
  auto_fit_->setChecked(true);
  auto_fit_->setToolTip(
      tr("Keep every axis long enough to cross the others: verticals span the grid's "
         "depth, horizontals span its width. Uncheck to type each axis's start / end."));
  connect(auto_fit_, &QCheckBox::toggled, this, [this](bool on) {
    update_extent_editability();
    if (on) {
      fit_axis_extents();
    }
  });
  fit_box->addWidget(auto_fit_);
  auto* fit_button = new QPushButton(tr("Fit to grid"), this);
  fit_button->setToolTip(tr("Re-span every axis to the current grid extent"));
  connect(fit_button, &QPushButton::clicked, this, [this] { fit_axis_extents(); });
  fit_box->addWidget(fit_button);
  fit_box->addStretch(1);
  root->addLayout(fit_box);

  auto* hint = new QLabel(
      tr("Generating replaces the table above. Positions are in metres, measured from the origin."),
      this);
  hint->setWordWrap(true);
  root->addWidget(hint);

  // 放置步骤：轴网是定位参考，落位点得由用户在模型里点——表里只有相对形状。
  place_with_click_ = new QCheckBox(tr("Place in the viewport with a mouse click"), this);
  place_with_click_->setChecked(true);
  place_with_click_->setToolTip(
      tr("After OK the grid follows the cursor; click to drop it. Uncheck to keep the "
         "table coordinates as they are."));
  root->addWidget(place_with_click_);

  place_hint_ = new QLabel(
      tr("Placement: the origin lands where you click and snaps to grid intersections. "
         "Esc cancels, and the whole grid is one undo step."),
      this);
  place_hint_->setWordWrap(true);
  root->addWidget(place_hint_);
  connect(place_with_click_, &QCheckBox::toggled, place_hint_, &QWidget::setVisible);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  // 按钮文字跟着勾选走：一眼能看出确定之后是"直接落位"还是"进放置步骤"。
  if (QPushButton* ok = buttons->button(QDialogButtonBox::Ok)) {
    const auto retitle = [this, ok](bool place) {
      ok->setText(place ? tr("OK and Place") : tr("OK"));
    };
    retitle(place_with_click_->isChecked());
    connect(place_with_click_, &QCheckBox::toggled, ok, retitle);
  }
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

  // 改方向 / 位置会让轴网范围变，勾了自动适配就顺手把端点拉齐。
  if (auto* position = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kPositionColumn))) {
    connect(position, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
            [this](double) { maybe_auto_fit(); });
  }
  if (auto* direction = qobject_cast<QComboBox*>(table_->cellWidget(row, kDirectionColumn))) {
    connect(direction, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { maybe_auto_fit(); });
  }
  update_extent_editability();
}

void GridSettingsDialog::add_empty_axis(GridAxisDirection direction) {
  GridAxis axis;
  axis.id = 0;  // 新增：保存时由 UpdateGridCommand 分配句柄
  axis.name = next_axis_name(direction).toStdString();
  axis.direction = direction;
  axis.position = next_axis_position(direction);
  axis.start = -kDefaultLength * 0.5;
  axis.end = kDefaultLength * 0.5;
  add_row(axis);
  table_->selectRow(table_->rowCount() - 1);
  maybe_auto_fit();
}

void GridSettingsDialog::fit_axis_extents() {
  std::vector<GridAxis> current = axes();
  if (current.empty()) {
    return;
  }
  fit_grid_axes_to_extent(current, margin_ != nullptr ? margin_->value() : 1.0, kDefaultLength);
  for (int row = 0; row < table_->rowCount() && row < static_cast<int>(current.size()); ++row) {
    auto* start = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kStartColumn));
    auto* end = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kEndColumn));
    if (start != nullptr) {
      start->setValue(current[row].start);
    }
    if (end != nullptr) {
      end->setValue(current[row].end);
    }
  }
}

void GridSettingsDialog::maybe_auto_fit() {
  if (auto_fit_ != nullptr && auto_fit_->isChecked()) {
    fit_axis_extents();
  }
}

void GridSettingsDialog::update_extent_editability() {
  const bool editable = auto_fit_ == nullptr || !auto_fit_->isChecked();
  for (int row = 0; row < table_->rowCount(); ++row) {
    if (auto* start = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kStartColumn))) {
      start->setEnabled(editable);
    }
    if (auto* end = qobject_cast<QDoubleSpinBox*>(table_->cellWidget(row, kEndColumn))) {
      end->setEnabled(editable);
    }
  }
}

double GridSettingsDialog::next_axis_position(GridAxisDirection direction) const {
  const double spacing = direction == GridAxisDirection::AlongZ ? kDefaultVerticalSpacing
                                                                : kDefaultHorizontalSpacing;
  bool any = false;
  double max_position = 0.0;
  for (int row = 0; row < table_->rowCount(); ++row) {
    const auto* combo = qobject_cast<const QComboBox*>(table_->cellWidget(row, kDirectionColumn));
    const auto* position =
        qobject_cast<const QDoubleSpinBox*>(table_->cellWidget(row, kPositionColumn));
    if (combo == nullptr || position == nullptr || combo_direction(combo) != direction) {
      continue;
    }
    if (!any || position->value() > max_position) {
      max_position = position->value();
      any = true;
    }
  }
  // 同方向还没有轴就落在原点；已有就往外挪一个默认间距，保证新轴不会跟人重叠。
  return any ? max_position + spacing : 0.0;
}

QString GridSettingsDialog::next_axis_name(GridAxisDirection direction) const {
  const auto name_taken = [this](const QString& candidate) {
    for (int row = 0; row < table_->rowCount(); ++row) {
      const QTableWidgetItem* item = table_->item(row, kNameColumn);
      if (item != nullptr && item->text() == candidate) {
        return true;
      }
    }
    return false;
  };
  if (direction == GridAxisDirection::AlongZ) {
    // 竖轴 = 编号轴：1、2、3…
    int serial = 1;
    while (name_taken(QString::number(serial))) {
      ++serial;
    }
    return QString::number(serial);
  }
  // 横轴 = 字母轴：A、B、C…（超过 26 根进位到 AA）。
  int letter = 0;
  while (name_taken(QString::fromStdString(grid_axis_letter_name(letter)))) {
    ++letter;
  }
  return QString::fromStdString(grid_axis_letter_name(letter));
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
  maybe_auto_fit();
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
  maybe_auto_fit();
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

bool GridSettingsDialog::place_with_click() const {
  return place_with_click_ == nullptr || place_with_click_->isChecked();
}

Vec2 GridSettingsDialog::placement_anchor() const {
  if (origin_x_ == nullptr || origin_z_ == nullptr) {
    return {};
  }
  return {static_cast<float>(origin_x_->value()), static_cast<float>(origin_z_->value())};
}

}  // namespace tamias
