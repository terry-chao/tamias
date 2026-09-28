#include "app/bim/floors/copy_floor_dialog.h"

#include <QAbstractItemView>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

#include <algorithm>

namespace tamias {

CopyFloorDialog::CopyFloorDialog(std::vector<Storey> storeys, std::uint64_t active_storey_id,
                                 QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("copyFloorDialog"));
  setWindowTitle(tr("Copy Floor"));
  setModal(true);
  setMinimumWidth(380);

  std::sort(storeys.begin(), storeys.end(),
            [](const Storey& a, const Storey& b) { return a.elevation < b.elevation; });

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  auto* form = new QFormLayout();
  form->setSpacing(8);
  source_ = new QComboBox(this);
  form->addRow(tr("Copy from"), source_);

  targets_ = new QListWidget(this);
  targets_->setObjectName(QStringLiteral("copyFloorTargets"));
  targets_->setSelectionMode(QAbstractItemView::ExtendedSelection);
  targets_->setSelectionBehavior(QAbstractItemView::SelectRows);
  targets_->setUniformItemSizes(true);
  targets_->setMinimumHeight(150);
  targets_->setToolTip(tr("Select one or more target floors"));
  form->addRow(tr("Copy to"), targets_);
  root->addLayout(form);

  hint_ = new QLabel(
      tr("Select one or more target floors. Hold Shift to select a continuous range, or "
         "Ctrl to add individual floors. Doors and windows come with their walls; heights "
         "keep their offset relative to the floor."),
      this);
  hint_->setWordWrap(true);
  hint_->setObjectName(QStringLiteral("copyFloorHint"));
  hint_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  root->addWidget(hint_);

  warning_ = new QLabel(tr("Pick a source floor and at least one target floor."), this);
  warning_->setObjectName(QStringLiteral("copyFloorWarning"));
  warning_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  warning_->setVisible(false);
  root->addWidget(warning_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &CopyFloorDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &CopyFloorDialog::reject);
  connect(source_, &QComboBox::currentIndexChanged, this,
          [this] { rebuild_targets(); });
  connect(targets_, &QListWidget::itemSelectionChanged, this,
          [this] { sync_ok_enabled(); });

  fill(storeys);

  // 默认：源 = 当前楼层（表里没有就第一层），目标 = 源上面的那一层，没有就下面那层。
  int source_row = 0;
  for (int row = 0; row < source_->count(); ++row) {
    if (static_cast<std::uint64_t>(source_->itemData(row).toULongLong()) ==
        active_storey_id) {
      source_row = row;
      break;
    }
  }
  {
    const QSignalBlocker blocker(source_);
    source_->setCurrentIndex(source_row);
  }
  rebuild_targets();
  sync_ok_enabled();
}

void CopyFloorDialog::fill(const std::vector<Storey>& storeys) {
  storeys_ = storeys;
  for (const Storey& storey : storeys_) {
    const QString name = QString::fromStdString(storey.name);
    const QString label = storey.mezzanine ? tr("[Mezzanine] %1").arg(name) : name;
    const auto id = static_cast<qulonglong>(storey.id);
    source_->addItem(label, id);
  }
}

void CopyFloorDialog::rebuild_targets() {
  const std::vector<std::uint64_t> previous = selected_target_storey_ids();
  const std::uint64_t source = source_storey_id();

  {
    const QSignalBlocker blocker(targets_);
    targets_->clear();
    QListWidgetItem* first_selected = nullptr;
    for (const Storey& storey : storeys_) {
      if (storey.id == source) {
        continue;
      }
      const QString name = QString::fromStdString(storey.name);
      const QString label = storey.mezzanine ? tr("[Mezzanine] %1").arg(name) : name;
      auto* item = new QListWidgetItem(label, targets_);
      item->setData(Qt::UserRole, static_cast<qulonglong>(storey.id));
      if (std::find(previous.begin(), previous.end(), storey.id) != previous.end()) {
        item->setSelected(true);
        if (first_selected == nullptr) {
          first_selected = item;
        }
      }
    }

    if (targets_->selectedItems().isEmpty() && targets_->count() > 0) {
      int source_index = -1;
      for (std::size_t i = 0; i < storeys_.size(); ++i) {
        if (storeys_[i].id == source) {
          source_index = static_cast<int>(i);
          break;
        }
      }
      const int adjacent = source_index < 0
                               ? 0
                               : (source_index + 1 < static_cast<int>(storeys_.size())
                                      ? source_index + 1
                                      : source_index - 1);
      const std::uint64_t default_id = storeys_[static_cast<std::size_t>(adjacent)].id;
      for (int row = 0; row < targets_->count(); ++row) {
        if (targets_->item(row)->data(Qt::UserRole).toULongLong() == default_id) {
          targets_->item(row)->setSelected(true);
          targets_->setCurrentItem(targets_->item(row));
          break;
        }
      }
    } else if (first_selected != nullptr) {
      targets_->setCurrentItem(first_selected);
    }
  }
  sync_ok_enabled();
}

std::uint64_t CopyFloorDialog::combo_storey_id(const QComboBox* combo) const {
  const int index = combo->currentIndex();
  return index < 0 ? 0 : static_cast<std::uint64_t>(combo->itemData(index).toULongLong());
}

std::uint64_t CopyFloorDialog::source_storey_id() const { return combo_storey_id(source_); }

std::vector<std::uint64_t> CopyFloorDialog::selected_target_storey_ids() const {
  std::vector<std::uint64_t> ids;
  if (targets_ == nullptr) {
    return ids;
  }
  for (int row = 0; row < targets_->count(); ++row) {
    const QListWidgetItem* item = targets_->item(row);
    if (item->isSelected()) {
      ids.push_back(static_cast<std::uint64_t>(item->data(Qt::UserRole).toULongLong()));
    }
  }
  return ids;
}

std::vector<std::uint64_t> CopyFloorDialog::target_storey_ids() const {
  return selected_target_storey_ids();
}

void CopyFloorDialog::sync_ok_enabled() {
  const std::uint64_t source = source_storey_id();
  const bool valid = source != 0 && !target_storey_ids().empty();
  if (ok_ != nullptr) {
    ok_->setEnabled(valid);
  }
  if (warning_ != nullptr) {
    warning_->setVisible(!valid);
  }
}

void CopyFloorDialog::accept() {
  const std::uint64_t source = source_storey_id();
  if (source == 0 || target_storey_ids().empty()) {
    return;  // 表里没有楼层或没选目标：确定按钮本来就是灰的
  }
  QDialog::accept();
}

}  // namespace tamias
