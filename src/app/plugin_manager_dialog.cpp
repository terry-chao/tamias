#include "plugin_manager_dialog.h"

#include "plugin/plugin_command.h"
#include "plugin/plugin_info.h"
#include "plugin/plugin_manager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QStyleHints>
#include <QVBoxLayout>

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

QString dialog_stylesheet(bool dark) {
  if (dark) {
    return QStringLiteral(
        "#pluginManagerDialog { background: #303030; }"
        "QLabel { color: #e0e0e0; font-size: 12px; }"
        "QLabel#pluginManagerHint { color: #9aa0a6; font-size: 11px; }"
        "QLabel#pluginManagerEmpty { color: #9aa0a6; font-size: 12px; }"
        "QListWidget#pluginManagerList {"
        "  background: #383838; color: #e8e8e8; border: 1px solid #2f2f2f;"
        "  outline: none; padding: 4px; font-size: 13px;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
        "}"
        "QListWidget#pluginManagerList::item { padding: 8px 10px; }"
        "QListWidget#pluginManagerList::item:hover { background: #3c3f41; }"
        "QListWidget#pluginManagerList::item:selected { background: #3d8bd4; color: #ffffff; }"
        "QDialogButtonBox QPushButton { min-width: 80px; padding: 5px 14px; }");
  }
  return QStringLiteral(
      "#pluginManagerDialog { background: #f0f0f0; }"
      "QLabel { color: #2a2a2a; font-size: 12px; }"
      "QLabel#pluginManagerHint { color: #6a6a6a; font-size: 11px; }"
      "QLabel#pluginManagerEmpty { color: #6a6a6a; font-size: 12px; }"
      "QListWidget#pluginManagerList {"
      "  background: #ffffff; color: #1f1f1f; border: 1px solid #d0d0d0;"
      "  outline: none; padding: 4px; font-size: 13px;"
      "  font-family: 'Segoe UI', 'Microsoft YaHei UI', sans-serif;"
      "}"
      "QListWidget#pluginManagerList::item { padding: 8px 10px; }"
      "QListWidget#pluginManagerList::item:hover { background: #e8e8e8; }"
      "QListWidget#pluginManagerList::item:selected { background: #2a6fb0; color: #ffffff; }"
      "QDialogButtonBox QPushButton { min-width: 80px; padding: 5px 14px; }");
}

int command_count(const PluginManager& manager, const std::string& plugin_id) {
  int n = 0;
  for (const auto& cmd : manager.commands()) {
    if (cmd.plugin_id == plugin_id) {
      ++n;
    }
  }
  return n;
}

}  // namespace

PluginManagerDialog::PluginManagerDialog(PluginManager& manager, QWidget* parent)
    : QDialog(parent), manager_(&manager) {
  setWindowTitle(tr("Plugin Manager"));
  setObjectName(QStringLiteral("pluginManagerDialog"));
  setModal(true);
  resize(460, 420);
  setMinimumSize(360, 280);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(16, 14, 16, 12);
  root->setSpacing(10);

  hint_ = new QLabel(
      tr("Check the plugins that should appear on the ribbon. Changes apply when you click OK."),
      this);
  hint_->setObjectName(QStringLiteral("pluginManagerHint"));
  hint_->setWordWrap(true);
  root->addWidget(hint_);

  list_ = new QListWidget(this);
  list_->setObjectName(QStringLiteral("pluginManagerList"));
  list_->setSelectionMode(QAbstractItemView::NoSelection);
  list_->setFocusPolicy(Qt::NoFocus);
  list_->setUniformItemSizes(true);
  root->addWidget(list_, 1);

  empty_ = new QLabel(tr("No plugins are currently loaded."), this);
  empty_->setObjectName(QStringLiteral("pluginManagerEmpty"));
  empty_->setAlignment(Qt::AlignCenter);
  empty_->setWordWrap(true);
  root->addWidget(empty_, 1);

  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  root->addWidget(buttons);

  populate_list();
  apply_stylesheet();

  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::unordered_set<std::string> PluginManagerDialog::hidden_loaded_ids() const {
  std::unordered_set<std::string> hidden;
  if (list_ == nullptr) {
    return hidden;
  }
  for (int i = 0; i < list_->count(); ++i) {
    const QListWidgetItem* item = list_->item(i);
    if (item == nullptr || item->checkState() == Qt::Checked) {
      continue;
    }
    hidden.insert(item->data(Qt::UserRole).toString().toStdString());
  }
  return hidden;
}

void PluginManagerDialog::populate_list() {
  list_->clear();
  const auto& plugins = manager_->plugins();
  const bool empty = plugins.empty();
  list_->setVisible(!empty);
  empty_->setVisible(empty);
  if (empty) {
    return;
  }
  for (const auto& plugin : plugins) {
    const QString title = QString::fromUtf8(plugin.title.data(), static_cast<int>(plugin.title.size()));
    const QString id = QString::fromUtf8(plugin.id.data(), static_cast<int>(plugin.id.size()));
    const int commands = command_count(*manager_, plugin.id);
    auto* item = new QListWidgetItem(list_);
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    item->setCheckState(manager_->is_visible(plugin.id) ? Qt::Checked : Qt::Unchecked);
    item->setData(Qt::UserRole, id);
    if (commands > 0) {
      item->setText(tr("%1  (%2)").arg(title).arg(commands));
    } else {
      item->setText(title);
    }
    item->setToolTip(id);
  }
}

void PluginManagerDialog::apply_stylesheet() {
  if (applying_stylesheet_) {
    return;
  }
  applying_stylesheet_ = true;
  setStyleSheet(dialog_stylesheet(is_dark_theme()));
  applying_stylesheet_ = false;
}

void PluginManagerDialog::changeEvent(QEvent* event) {
  if (!applying_stylesheet_ &&
      (event->type() == QEvent::ThemeChange || event->type() == QEvent::PaletteChange)) {
    apply_stylesheet();
  }
  QDialog::changeEvent(event);
}

}  // namespace tamias
