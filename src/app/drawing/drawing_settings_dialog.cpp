#include "app/drawing/drawing_settings_dialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace tamias {
namespace {

QDoubleSpinBox* make_spin(QWidget* parent, double value, double step, int decimals,
                          const QString& suffix) {
  auto* spin = new QDoubleSpinBox(parent);
  spin->setRange(-1.0e7, 1.0e7);
  spin->setDecimals(decimals);
  spin->setSingleStep(step);
  spin->setSuffix(suffix);
  spin->setKeyboardTracking(false);
  spin->setValue(value);
  return spin;
}

}  // namespace

DrawingSettingsDialog::DrawingSettingsDialog(const DrawingRef& drawing, int page_count,
                                             double declared_unit_scale,
                                             FitProvider fit_provider, QWidget* parent)
    : QDialog(parent),
      unit_scale_(declared_unit_scale),
      fit_provider_(std::move(fit_provider)) {
  setObjectName(QStringLiteral("drawingSettingsDialog"));
  setWindowTitle(tr("Drawing Settings"));
  setModal(true);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 14, 14, 12);
  root->setSpacing(10);

  const QFileInfo info(QString::fromStdString(drawing.path));
  auto* title = new QLabel(info.fileName(), this);
  title->setStyleSheet(QStringLiteral("font-weight: 600;"));
  root->addWidget(title);
  if (!info.exists()) {
    auto* missing = new QLabel(tr("The file is gone — the drawing cannot be shown."), this);
    missing->setWordWrap(true);
    missing->setStyleSheet(QStringLiteral("color: #d93025;"));
    root->addWidget(missing);
  }

  auto* view_box = new QGroupBox(tr("Viewport"), this);
  auto* view_form = new QFormLayout(view_box);
  view_form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  visible_box_ = new QCheckBox(tr("Show this drawing in the viewport"), view_box);
  visible_box_->setChecked(drawing.visible);
  view_form->addRow(QString(), visible_box_);
  page_spin_ = new QSpinBox(view_box);
  page_spin_->setRange(1, std::max(1, page_count));
  page_spin_->setValue(std::clamp(drawing.page + 1, 1, std::max(1, page_count)));
  page_spin_->setEnabled(page_count > 1);
  view_form->addRow(tr("Page"), page_spin_);
  root->addWidget(view_box);

  auto* place_box = new QGroupBox(tr("Placement in the model"), this);
  auto* form = new QFormLayout(place_box);
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  scale_spin_ = make_spin(place_box, drawing.placement.scale, 0.001, 6,
                          tr(" m / drawing unit"));
  scale_spin_->setRange(1e-9, 1.0e7);
  rotation_spin_ = make_spin(place_box, drawing.placement.rotation_deg, 1.0, 3,
                             QStringLiteral(" °"));
  rotation_spin_->setRange(-360.0, 360.0);
  elevation_spin_ = make_spin(place_box, drawing.placement.elevation, 0.1, 3,
                              QStringLiteral(" m"));
  offset_x_spin_ = make_spin(place_box, drawing.placement.offset_x, 0.1, 3,
                             QStringLiteral(" m"));
  offset_z_spin_ = make_spin(place_box, drawing.placement.offset_z, 0.1, 3,
                             QStringLiteral(" m"));
  form->addRow(tr("Scale"), scale_spin_);
  form->addRow(tr("Rotation"), rotation_spin_);
  form->addRow(tr("Elevation (Y)"), elevation_spin_);
  form->addRow(tr("Offset X"), offset_x_spin_);
  form->addRow(tr("Offset Z"), offset_z_spin_);
  root->addWidget(place_box);

  auto* tools = new QHBoxLayout();
  tools->setSpacing(8);
  auto* fit_button = new QPushButton(tr("Fit to Model"), this);
  fit_button->setToolTip(
      tr("Scale the drawing by its declared units (or to the model extent) and centre it "
         "on the model / grid"));
  auto* unit_button = new QPushButton(tr("Use Drawing Units"), this);
  unit_button->setToolTip(tr("1 drawing unit → metres, taken from the DXF $INSUNITS header"));
  unit_button->setEnabled(declared_unit_scale > 0.0);
  tools->addWidget(fit_button);
  tools->addWidget(unit_button);
  tools->addStretch(1);
  root->addLayout(tools);

  hint_ = new QLabel(
      tr("The drawing plane is horizontal: the drawing origin lands at (Offset X, "
         "Elevation, Offset Z), the drawing's right side runs along +X and its top runs "
         "towards +Z (the same mapping the tracer uses). Rotation is anticlockwise seen from "
         "above. Scale is metres per drawing unit — 0.001 for a millimetre drawing. "
         "\"Fit to Model\" is the quickest way to get it approximately right."),
      this);
  hint_->setWordWrap(true);
  hint_->setStyleSheet(QStringLiteral("color: palette(mid); font-size: 11px;"));
  root->addWidget(hint_);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  root->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, this, &DrawingSettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &DrawingSettingsDialog::reject);
  connect(fit_button, &QPushButton::clicked, this, &DrawingSettingsDialog::apply_fit);
  connect(unit_button, &QPushButton::clicked, this,
          &DrawingSettingsDialog::reset_to_unit_scale);
}

DrawingPlacement DrawingSettingsDialog::placement() const {
  DrawingPlacement placement;
  placement.scale = scale_spin_->value();
  placement.rotation_deg = rotation_spin_->value();
  placement.elevation = elevation_spin_->value();
  placement.offset_x = offset_x_spin_->value();
  placement.offset_z = offset_z_spin_->value();
  return placement;
}

int DrawingSettingsDialog::page() const { return page_spin_->value() - 1; }

bool DrawingSettingsDialog::visible_in_viewport() const { return visible_box_->isChecked(); }

void DrawingSettingsDialog::apply_fit() {
  if (!fit_provider_) {
    return;
  }
  const std::optional<DrawingPlacement> fit = fit_provider_(page());
  if (!fit.has_value()) {
    QMessageBox::information(this, tr("Drawing Settings"),
                             tr("Cannot compute a placement — the drawing could not be "
                                "read."));
    return;
  }
  scale_spin_->setValue(fit->scale);
  rotation_spin_->setValue(fit->rotation_deg);
  elevation_spin_->setValue(fit->elevation);
  offset_x_spin_->setValue(fit->offset_x);
  offset_z_spin_->setValue(fit->offset_z);
}

void DrawingSettingsDialog::reset_to_unit_scale() {
  if (unit_scale_ > 0.0) {
    scale_spin_->setValue(unit_scale_);
  }
}

}  // namespace tamias
