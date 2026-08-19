#include "settings_dialog.h"

#include "app_settings.h"
#include "i18n.h"
#include "settings_section.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyleHints>
#include <QVBoxLayout>

#include <initializer_list>

namespace tamias {
namespace {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    switch (hints->colorScheme()) {
      case Qt::ColorScheme::Dark:
        return true;
      case Qt::ColorScheme::Light:
        return false;
      case Qt::ColorScheme::Unknown:
        break;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString settings_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#settingsDialog { background: #303030; }"
        "QListWidget#settingsNav {"
        "  background: #2b2d30; border: none; outline: none; padding: 6px 0;"
        "  font-size: 13px; font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QListWidget#settingsNav::item {"
        "  color: #dcdcdc; padding: 7px 14px; margin: 0;"
        "}"
        "QListWidget#settingsNav::item:hover { background: #3c3f41; }"
        "QListWidget#settingsNav::item:selected {"
        "  background: #3d8bd4; color: #ffffff;"
        "}"
        "QFrame#settingsNavSep { background: #3c3f41; border: none; max-width: 1px; }"
        "QScrollArea#settingsPageScroll, QWidget#settingsPage {"
        "  background: #303030; border: none;"
        "}"
        "QFrame#settingsSection { background: #383838; border: none; }"
        "QPushButton#settingsSectionHeader {"
        "  background: #424242; color: #e8e8e8; border: none;"
        "  padding: 5px 8px; text-align: left; font-weight: 600; font-size: 12px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QPushButton#settingsSectionHeader:hover { background: #4a4a4a; }"
        "QPushButton#settingsSectionHeader:checked { background: #424242; }"
        "QWidget#settingsSectionBody { background: #383838; }"
        "QLabel { color: #e0e0e0; font-size: 12px; }"
        "QLabel#settingsHint { color: #9aa0a6; font-size: 11px; }"
        "QComboBox {"
        "  background: #545454; color: #e8e8e8; border: 1px solid #2f2f2f;"
        "  padding: 3px 8px; min-height: 22px; min-width: 160px;"
        "}"
        "QComboBox::drop-down { border: none; width: 18px; }"
        "QComboBox QAbstractItemView {"
        "  background: #3c3f41; color: #e8e8e8; selection-background-color: #3d8bd4;"
        "  border: 1px solid #2f2f2f;"
        "}"
        "QCheckBox { color: #e0e0e0; spacing: 8px; }"
        "QDialogButtonBox QPushButton {"
        "  min-width: 80px; padding: 5px 14px;"
        "}");
  }

  return QStringLiteral(
      "#settingsDialog { background: #f0f0f0; }"
      "QListWidget#settingsNav {"
      "  background: #e8e8e8; border: none; outline: none; padding: 6px 0;"
      "  font-size: 13px; font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QListWidget#settingsNav::item {"
      "  color: #2a2a2a; padding: 7px 14px; margin: 0;"
      "}"
      "QListWidget#settingsNav::item:hover { background: #dcdcdc; }"
      "QListWidget#settingsNav::item:selected {"
      "  background: #2a6fb0; color: #ffffff;"
      "}"
      "QFrame#settingsNavSep { background: #c8c8c8; border: none; max-width: 1px; }"
      "QScrollArea#settingsPageScroll, QWidget#settingsPage {"
      "  background: #f0f0f0; border: none;"
      "}"
      "QFrame#settingsSection { background: #ffffff; border: 1px solid #d0d0d0; }"
      "QPushButton#settingsSectionHeader {"
      "  background: #e6e6e6; color: #1f1f1f; border: none;"
      "  padding: 5px 8px; text-align: left; font-weight: 600; font-size: 12px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QPushButton#settingsSectionHeader:hover { background: #dddddd; }"
      "QPushButton#settingsSectionHeader:checked { background: #e6e6e6; }"
      "QWidget#settingsSectionBody { background: #ffffff; }"
      "QLabel { color: #2a2a2a; font-size: 12px; }"
      "QLabel#settingsHint { color: #6a6a6a; font-size: 11px; }"
      "QComboBox {"
      "  background: #ffffff; color: #1f1f1f; border: 1px solid #bdbdbd;"
      "  padding: 3px 8px; min-height: 22px; min-width: 160px;"
      "}"
      "QCheckBox { color: #2a2a2a; spacing: 8px; }"
      "QDialogButtonBox QPushButton {"
      "  min-width: 80px; padding: 5px 14px;"
      "}");
}

QScrollArea* wrap_page(QWidget* inner) {
  inner->setObjectName(QStringLiteral("settingsPage"));
  auto* scroll = new QScrollArea();
  scroll->setObjectName(QStringLiteral("settingsPageScroll"));
  scroll->setWidgetResizable(true);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setWidget(inner);
  return scroll;
}

QWidget* make_page(std::initializer_list<QWidget*> sections) {
  auto* inner = new QWidget();
  auto* layout = new QVBoxLayout(inner);
  layout->setContentsMargins(12, 10, 14, 10);
  layout->setSpacing(8);
  for (QWidget* section : sections) {
    layout->addWidget(section);
  }
  layout->addStretch(1);
  return inner;
}

}  // namespace

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
  setWindowTitle(tr("Preferences"));
  setObjectName(QStringLiteral("settingsDialog"));
  setModal(true);
  resize(820, 560);
  setMinimumSize(640, 420);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  nav_ = new QListWidget(this);
  nav_->setObjectName(QStringLiteral("settingsNav"));
  nav_->setFixedWidth(176);
  nav_->setFocusPolicy(Qt::NoFocus);
  nav_->setFrameShape(QFrame::NoFrame);
  nav_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  nav_->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  nav_->setSpacing(0);

  auto* stack = new QStackedWidget(this);

  language_combo_ = new QComboBox(this);
  for (const QString& code : available_ui_languages()) {
    language_combo_->addItem(ui_language_display_name(code), code);
  }
  const int lang_index =
      language_combo_->findData(AppSettings::instance().ui_language());
  language_combo_->setCurrentIndex(lang_index >= 0 ? lang_index : 0);

  auto* translation_section = new SettingsSection(tr("Translation"), this);
  translation_section->add_row(tr("Language"), language_combo_);

  theme_combo_ = new QComboBox(this);
  theme_combo_->addItem(tr("System"), static_cast<int>(UiColorScheme::System));
  theme_combo_->addItem(tr("Light"), static_cast<int>(UiColorScheme::Light));
  theme_combo_->addItem(tr("Dark"), static_cast<int>(UiColorScheme::Dark));
  const int theme_index =
      theme_combo_->findData(static_cast<int>(AppSettings::instance().ui_color_scheme()));
  theme_combo_->setCurrentIndex(theme_index >= 0 ? theme_index : 0);

  auto* theme_section = new SettingsSection(tr("Color"), this);
  theme_section->add_row(tr("Appearance"), theme_combo_);

  zoom_to_mouse_check_ = new QCheckBox(this);
  zoom_to_mouse_check_->setChecked(AppSettings::instance().zoom_to_mouse_position());
  zoom_to_mouse_check_->setMinimumHeight(22);

  auto* zoom_section = new SettingsSection(tr("Zoom"), this);
  zoom_section->add_row(tr("Zoom to Mouse Position"), zoom_to_mouse_check_);

  backend_combo_ = new QComboBox(this);
  backend_combo_->addItem(tr("Vulkan"), static_cast<int>(GraphicsBackend::Vulkan));
  backend_combo_->addItem(tr("OpenGL"), static_cast<int>(GraphicsBackend::OpenGL));
  const auto current = AppSettings::instance().graphics_backend();
  const int index = backend_combo_->findData(static_cast<int>(current));
  backend_combo_->setCurrentIndex(index >= 0 ? index : 0);

  backend_hint_ = new QLabel(this);
  backend_hint_->setWordWrap(true);
  backend_hint_->setObjectName(QStringLiteral("settingsHint"));

  auto* graphics_section = new SettingsSection(tr("Graphics"), this);
  graphics_section->add_row(tr("Render backend"), backend_combo_);
  graphics_section->add_row(backend_hint_);

  stack->addWidget(wrap_page(make_page({translation_section})));
  stack->addWidget(wrap_page(make_page({theme_section})));
  stack->addWidget(wrap_page(make_page({zoom_section})));
  stack->addWidget(wrap_page(make_page({graphics_section})));

  auto* body = new QWidget(this);
  auto* body_layout = new QHBoxLayout(body);
  body_layout->setContentsMargins(0, 0, 0, 0);
  body_layout->setSpacing(0);
  body_layout->addWidget(nav_);
  auto* sep = new QFrame(body);
  sep->setObjectName(QStringLiteral("settingsNavSep"));
  sep->setFrameShape(QFrame::VLine);
  sep->setFixedWidth(1);
  body_layout->addWidget(sep);
  body_layout->addWidget(stack, 1);
  root->addWidget(body, 1);

  auto* buttons_bar = new QWidget(this);
  auto* buttons_layout = new QHBoxLayout(buttons_bar);
  buttons_layout->setContentsMargins(12, 8, 12, 10);
  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, buttons_bar);
  buttons_layout->addStretch(1);
  buttons_layout->addWidget(buttons);
  root->addWidget(buttons_bar);

  populate_categories();
  apply_stylesheet();

  connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(backend_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
          &SettingsDialog::on_backend_changed);
  connect(nav_, &QListWidget::currentRowChanged, stack, &QStackedWidget::setCurrentIndex);

  on_backend_changed(backend_combo_->currentIndex());
  nav_->setCurrentRow(2);
}

void SettingsDialog::populate_categories() {
  nav_->clear();
  nav_->addItem(tr("Interface"));
  nav_->addItem(tr("Themes"));
  nav_->addItem(tr("Navigation"));
  nav_->addItem(tr("System", "settings category"));
}

void SettingsDialog::apply_stylesheet() {
  if (applying_stylesheet_) {
    return;
  }
  applying_stylesheet_ = true;
  setStyleSheet(settings_stylesheet(is_dark_theme()));
  applying_stylesheet_ = false;
}

void SettingsDialog::changeEvent(QEvent* event) {
  if (!applying_stylesheet_ &&
      (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange)) {
    apply_stylesheet();
  }
  QDialog::changeEvent(event);
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
  settings.set_zoom_to_mouse_position(zoom_to_mouse_check_->isChecked());
  settings.save();
  if (theme_changed_) {
    apply_ui_color_scheme(theme);
  }
  QDialog::accept();
}

}  // namespace tamias
