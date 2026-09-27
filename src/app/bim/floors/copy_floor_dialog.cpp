#include "app/bim/floors/copy_floor_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace tamias {

CopyFloorDialog::CopyFloorDialog(std::vector<Storey> storeys, std::uint64_t active_storey_id,
                                 QWidget* parent)
    : QDialog(parent) {
  setObjectName(QStringLiteral("copyFloorDialog"));
  setWindowTitle(tr("Copy Floor"));
  setModal(true);
  setMinimumWidth(360);

  std::sort(storeys.begin(), storeys.end(),
            [](const Storey& a, const Storey& b) { return a.elevation < b.elevation; });

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  auto* form = new QFormLayout();
  form->setSpacing(8);
  source_ = new QComboBox(this);
  target_ = new QComboBox(this);
  form->addRow(tr("Copy from"), source_);
  form->addRow(tr("Copy to"), target_);
  root->addLayout(form);

  hint_ = new QLabel(
      tr("Every component on the source floor is copied to the target floor as a new "
         "component. Doors and windows come with their walls; heights keep their offset "
         "relative to the floor."),
      this);
  hint_->setWordWrap(true);
  hint_->setObjectName(QStringLiteral("copyFloorHint"));
  hint_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  root->addWidget(hint_);

  warning_ = new QLabel(tr("Pick two different floors."), this);
  warning_->setObjectName(QStringLiteral("copyFloorWarning"));
  warning_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  warning_->setVisible(false);
  root->addWidget(warning_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  ok_ = buttons->button(QDialogButtonBox::Ok);
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &CopyFloorDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &CopyFloorDialog::reject);
  connect(source_, &QComboBox::currentIndexChanged, this, [this] { sync_ok_enabled(); });
  connect(target_, &QComboBox::currentIndexChanged, this, [this] { sync_ok_enabled(); });

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
  source_->setCurrentIndex(source_row);
  if (target_->count() > 1) {
    target_->setCurrentIndex(source_row + 1 < target_->count() ? source_row + 1
                                                               : source_row - 1);
  }
  sync_ok_enabled();
}

void CopyFloorDialog::fill(const std::vector<Storey>& storeys) {
  for (const Storey& storey : storeys) {
    const QString name = QString::fromStdString(storey.name);
    const QString label = storey.mezzanine ? tr("[Mezzanine] %1").arg(name) : name;
    const auto id = static_cast<qulonglong>(storey.id);
    source_->addItem(label, id);
    target_->addItem(label, id);
  }
}

std::uint64_t CopyFloorDialog::combo_storey_id(const QComboBox* combo) const {
  const int index = combo->currentIndex();
  return index < 0 ? 0 : static_cast<std::uint64_t>(combo->itemData(index).toULongLong());
}

std::uint64_t CopyFloorDialog::source_storey_id() const { return combo_storey_id(source_); }

std::uint64_t CopyFloorDialog::target_storey_id() const { return combo_storey_id(target_); }

void CopyFloorDialog::sync_ok_enabled() {
  const std::uint64_t source = source_storey_id();
  const std::uint64_t target = target_storey_id();
  const bool valid = source != 0 && target != 0 && source != target;
  if (ok_ != nullptr) {
    ok_->setEnabled(valid);
  }
  if (warning_ != nullptr) {
    warning_->setVisible(!valid);
  }
}

void CopyFloorDialog::accept() {
  const std::uint64_t source = source_storey_id();
  const std::uint64_t target = target_storey_id();
  if (source == 0 || target == 0) {
    return;  // 表里没有楼层：确定按钮本来就是灰的
  }
  if (source == target) {
    return;  // 同一层：提示已经摆在下面，别关对话框
  }
  QDialog::accept();
}

}  // namespace tamias
