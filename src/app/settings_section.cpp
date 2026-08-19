#include "settings_section.h"

#include <QFormLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

namespace tamias {

SettingsSection::SettingsSection(const QString& title, QWidget* parent) : QFrame(parent) {
  setObjectName(QStringLiteral("settingsSection"));
  setFrameShape(QFrame::NoFrame);
  setAttribute(Qt::WA_StyledBackground, true);
  title_ = title;

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  header_ = new QPushButton(this);
  header_->setObjectName(QStringLiteral("settingsSectionHeader"));
  header_->setCheckable(true);
  header_->setChecked(true);
  header_->setFlat(true);
  header_->setCursor(Qt::PointingHandCursor);
  header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  header_->setMinimumHeight(26);
  header_->setFocusPolicy(Qt::NoFocus);
  set_header_text(true);

  body_ = new QWidget(this);
  body_->setObjectName(QStringLiteral("settingsSectionBody"));
  form_ = new QFormLayout(body_);
  form_->setContentsMargins(18, 10, 16, 12);
  form_->setHorizontalSpacing(20);
  form_->setVerticalSpacing(8);
  form_->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
  form_->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
  form_->setFieldGrowthPolicy(QFormLayout::FieldsStayAtSizeHint);

  root->addWidget(header_);
  root->addWidget(body_);

  connect(header_, &QPushButton::toggled, this, &SettingsSection::on_toggled);
}

void SettingsSection::add_row(const QString& label, QWidget* field) {
  form_->addRow(label, field);
}

void SettingsSection::add_row(QWidget* widget) {
  form_->addRow(widget);
}

void SettingsSection::set_header_text(bool expanded) {
  header_->setText((expanded ? QStringLiteral("▼  ") : QStringLiteral("▶  ")) + title_);
}

void SettingsSection::on_toggled(bool expanded) {
  body_->setVisible(expanded);
  set_header_text(expanded);
}

}  // namespace tamias
