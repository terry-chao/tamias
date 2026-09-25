#include "app/edit/array_dialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

namespace tamias {

ArrayDialog::ArrayDialog(QWidget* parent, double centre_x, double centre_z) : QDialog(parent) {
  setWindowTitle(tr("Array"));
  setModal(true);

  mode_ = new QComboBox(this);
  mode_->addItem(tr("Linear (spacing along a direction)"), false);
  mode_->addItem(tr("Polar (rotate around the selection centre)"), true);

  count_ = new QSpinBox(this);
  count_->setRange(2, 200);
  count_->setValue(3);
  count_->setSuffix(tr(" items"));

  distance_ = new QDoubleSpinBox(this);
  distance_->setDecimals(3);
  distance_->setRange(0.001, 1.0e6);
  distance_->setValue(1.0);
  distance_->setKeyboardTracking(false);

  distance_label_ = new QLabel(tr("Spacing"), this);

  centre_x_ = new QDoubleSpinBox(this);
  centre_x_->setDecimals(3);
  centre_x_->setRange(-1.0e6, 1.0e6);
  centre_x_->setSingleStep(0.5);
  centre_x_->setKeyboardTracking(false);
  centre_x_->setValue(centre_x);
  centre_z_ = new QDoubleSpinBox(this);
  centre_z_->setDecimals(3);
  centre_z_->setRange(-1.0e6, 1.0e6);
  centre_z_->setSingleStep(0.5);
  centre_z_->setKeyboardTracking(false);
  centre_z_->setValue(centre_z);
  centre_label_ = new QLabel(tr("Centre X / Z"), this);

  auto* centre_row = new QWidget(this);
  auto* centre_layout = new QHBoxLayout(centre_row);
  centre_layout->setContentsMargins(0, 0, 0, 0);
  centre_layout->addWidget(centre_x_);
  centre_layout->addWidget(centre_z_);

  auto* form = new QFormLayout;
  form->addRow(tr("Mode"), mode_);
  form->addRow(tr("Count"), count_);
  form->addRow(distance_label_, distance_);
  form->addRow(centre_label_, centre_row);

  hint_ = new QLabel(this);
  hint_->setWordWrap(true);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  auto* layout = new QVBoxLayout(this);
  layout->addLayout(form);
  layout->addWidget(hint_);
  layout->addWidget(buttons);

  connect(mode_, &QComboBox::currentIndexChanged, this, [this] { sync_fields(); });
  sync_fields();
}

void ArrayDialog::sync_fields() {
  const bool polar = mode_->currentData().toBool();
  distance_label_->setText(polar ? tr("Angle per copy") : tr("Spacing"));
  centre_label_->setEnabled(polar);
  centre_x_->setEnabled(polar);
  centre_z_->setEnabled(polar);
  distance_->setSuffix(polar ? QStringLiteral(" °") : QStringLiteral(" m"));
  if (polar) {
    distance_->setRange(0.001, 360.0);
    if (distance_->value() > 360.0 || distance_->value() == 1.0) {
      distance_->setValue(15.0);
    }
    hint_->setText(tr("Each copy is rotated around the centre below (seeded with the centre "
                      "of the current selection)."));
  } else {
    distance_->setRange(0.001, 1.0e6);
    if (distance_->value() == 15.0) {
      distance_->setValue(1.0);
    }
    hint_->setText(tr("Copies are laid out along world +X. Select one object and use the "
                      "Move/Copy tool if you need another direction."));
  }
}

ArrayDialog::Params ArrayDialog::params() const {
  Params out;
  out.polar = mode_->currentData().toBool();
  out.count = count_->value();
  if (out.polar) {
    out.step_angle = distance_->value();
    out.centre_x = centre_x_->value();
    out.centre_z = centre_z_->value();
  } else {
    out.spacing = distance_->value();
  }
  return out;
}

}  // namespace tamias
