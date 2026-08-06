#include "settings_dialog.h"

#include "app_settings.h"
#include "i18n.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace tamias {

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Settings"));
  setModal(true);
  resize(420, 260);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(18, 16, 18, 14);
  root->setSpacing(14);

  auto* form = new QFormLayout();
  form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
  form->setHorizontalSpacing(16);
  form->setVerticalSpacing(10);

  language_combo_ = new QComboBox(this);
  for (const QString& code : available_ui_languages()) {
    language_combo_->addItem(ui_language_display_name(code), code);
  }
  const int lang_index =
      language_combo_->findData(AppSettings::instance().ui_language());
  language_combo_->setCurrentIndex(lang_index >= 0 ? lang_index : 0);
  form->addRow(tr("Language:"), language_combo_);

  theme_combo_ = new QComboBox(this);
  theme_combo_->addItem(tr("System"), static_cast<int>(UiColorScheme::System));
  theme_combo_->addItem(tr("Light"), static_cast<int>(UiColorScheme::Light));
  theme_combo_->addItem(tr("Dark"), static_cast<int>(UiColorScheme::Dark));
  const int theme_index =
      theme_combo_->findData(static_cast<int>(AppSettings::instance().ui_color_scheme()));
  theme_combo_->setCurrentIndex(theme_index >= 0 ? theme_index : 0);
  form->addRow(tr("Appearance:"), theme_combo_);

  backend_combo_ = new QComboBox(this);
  backend_combo_->addItem(tr("Vulkan"), static_cast<int>(GraphicsBackend::Vulkan));
  backend_combo_->addItem(tr("OpenGL"), static_cast<int>(GraphicsBackend::OpenGL));

  const auto current = AppSettings::instance().graphics_backend();
  const int index = backend_combo_->findData(static_cast<int>(current));
  backend_combo_->setCurrentIndex(index >= 0 ? index : 0);
  form->addRow(tr("Render backend:"), backend_combo_);
  root->addLayout(form);

  backend_hint_ = new QLabel(this);
  backend_hint_->setWordWrap(true);
  backend_hint_->setObjectName(QStringLiteral("settingsHint"));
  root->addWidget(backend_hint_);
  root->addStretch(1);

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  root->addWidget(buttons);

  connect(backend_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SettingsDialog::on_backend_changed);
  on_backend_changed(backend_combo_->currentIndex());
}

void SettingsDialog::on_backend_changed(int index) {
  const auto backend =
      static_cast<GraphicsBackend>(backend_combo_->itemData(index).toInt());
  if (backend == GraphicsBackend::OpenGL) {
    backend_hint_->setText(
        tr("OpenGL uses an isolated render thread per document (never shared). "
           "Backend changes take effect after restarting Tamias."));
  } else {
    backend_hint_->setText(
        tr("Vulkan documents with the same settings share one render thread. "
           "Backend changes take effect after restarting Tamias."));
  }
}

void SettingsDialog::accept() {
  const auto backend =
      static_cast<GraphicsBackend>(backend_combo_->currentData().toInt());
  const QString language = language_combo_->currentData().toString();
  const auto theme =
      static_cast<UiColorScheme>(theme_combo_->currentData().toInt());
  auto& settings = AppSettings::instance();
  language_changed_ = language != settings.ui_language();
  backend_changed_ = backend != settings.graphics_backend();
  theme_changed_ = theme != settings.ui_color_scheme();
  settings.set_graphics_backend(backend);
  settings.set_ui_language(language);
  settings.set_ui_color_scheme(theme);
  settings.save();
  if (theme_changed_) {
    apply_ui_color_scheme(theme);
  }
  QDialog::accept();
}

}  // namespace tamias
