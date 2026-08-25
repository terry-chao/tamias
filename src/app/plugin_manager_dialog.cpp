#include "plugin_manager_dialog.h"

#include "plugin/plugin_command.h"
#include "plugin/plugin_info.h"
#include "plugin/plugin_manager.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPixmap>
#include <QPushButton>
#include <QSize>
#include <QStyleHints>
#include <QTabWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace tamias {
namespace {

bool is_dark_theme() {
  if (const QStyleHints* hints = QGuiApplication::styleHints()) {
    if (hints->colorScheme() == Qt::ColorScheme::Dark) {
      return true;
    }
    if (hints->colorScheme() == Qt::ColorScheme::Light) {
      return false;
    }
  }
  return QApplication::palette().color(QPalette::Window).lightness() < 128;
}

QString dialog_stylesheet(bool dark) {
  const QString panel = dark ? QStringLiteral("#383838")
                             : QStringLiteral("#ffffff");
  const QString border = dark ? QStringLiteral("#505050")
                              : QStringLiteral("#d0d0d0");
  const QString text = dark ? QStringLiteral("#e8e8e8")
                            : QStringLiteral("#202020");
  const QString muted = dark ? QStringLiteral("#a8adb3")
                             : QStringLiteral("#666666");
  return QStringLiteral(
             "#pluginManagerDialog { background: %1; color: %2; }"
             "#pluginList, #pluginCommandList, #ribbonOrderList {"
             " background: %3; color: %2; border: 1px solid %4;"
             " outline: none; padding: 4px; }"
             "#pluginList::item, #pluginCommandList::item,"
             " #ribbonOrderList::item { padding: 7px 8px; }"
             "#pluginTitle { font-size: 18px; font-weight: 600; }"
             "#pluginBadge { color: %5; font-weight: 600; }"
             "#pluginMetadata, #pluginDescription, #pluginEmpty,"
             " #pluginManagerHint { color: %5; }"
             "QPushButton { min-width: 78px; padding: 5px 12px; }")
      .arg(dark ? QStringLiteral("#303030") : QStringLiteral("#f3f3f3"),
           text, panel, border, muted);
}

QString to_qstring(const std::string& text) {
  return QString::fromUtf8(text.data(), static_cast<int>(text.size()));
}

QString location_title(const std::string& id) {
  const char* source = nullptr;
  if (id == "home") source = "Home";
  if (id == "draw") source = "Draw";
  if (id == "plugins") source = "Plugins";
  if (id == "commands") source = "Commands";
  if (id == "view") source = "View";
  if (id == "panels") source = "Panels";
  if (id == "workspace") source = "Workspace";
  if (id == "manage") source = "Manage";
  return source == nullptr
             ? to_qstring(id)
             : QCoreApplication::translate("tamias::PluginManagerDialog",
                                           source);
}

const PluginInfo* find_plugin(const PluginManager& manager,
                              const std::string& id) {
  for (const PluginInfo& plugin : manager.plugins()) {
    if (plugin.id == id) {
      return &plugin;
    }
  }
  return nullptr;
}

const PluginCommand* find_command(const PluginManager& manager,
                                  const std::string& id) {
  for (const PluginCommand& command : manager.commands()) {
    if (command.id == id) {
      return &command;
    }
  }
  return nullptr;
}

}  // namespace

PluginManagerDialog::PluginManagerDialog(PluginManager& manager,
                                         QWidget* parent)
    : QDialog(parent), manager_(&manager),
      working_order_(manager.command_order()) {
  setWindowTitle(tr("Plugin Manager"));
  setObjectName(QStringLiteral("pluginManagerDialog"));
  setModal(true);
  resize(860, 580);
  setMinimumSize(700, 480);

  auto* root = new QVBoxLayout(this);
  root->setContentsMargins(14, 12, 14, 12);
  root->setSpacing(10);

  auto* hint = new QLabel(
      tr("Enable plugins and arrange plugin commands on the ribbon. "
         "Changes apply when you click OK."),
      this);
  hint->setObjectName(QStringLiteral("pluginManagerHint"));
  hint->setWordWrap(true);
  root->addWidget(hint);

  auto* tabs = new QTabWidget(this);
  root->addWidget(tabs, 1);

  auto* installed = new QWidget(tabs);
  auto* installed_layout = new QHBoxLayout(installed);
  installed_layout->setContentsMargins(8, 8, 8, 8);
  installed_layout->setSpacing(14);
  plugin_list_ = new QListWidget(installed);
  plugin_list_->setObjectName(QStringLiteral("pluginList"));
  plugin_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  plugin_list_->setIconSize(QSize(32, 32));
  plugin_list_->setMinimumWidth(260);
  installed_layout->addWidget(plugin_list_, 0);

  auto* details = new QWidget(installed);
  auto* details_layout = new QVBoxLayout(details);
  details_layout->setContentsMargins(4, 2, 4, 2);
  details_layout->setSpacing(8);
  auto* title_row = new QHBoxLayout();
  plugin_icon_ = new QLabel(details);
  plugin_icon_->setFixedSize(48, 48);
  plugin_icon_->setAlignment(Qt::AlignCenter);
  title_row->addWidget(plugin_icon_);
  plugin_title_ = new QLabel(details);
  plugin_title_->setObjectName(QStringLiteral("pluginTitle"));
  title_row->addWidget(plugin_title_);
  plugin_badge_ = new QLabel(details);
  plugin_badge_->setObjectName(QStringLiteral("pluginBadge"));
  title_row->addWidget(plugin_badge_);
  title_row->addStretch(1);
  details_layout->addLayout(title_row);

  plugin_metadata_ = new QLabel(details);
  plugin_metadata_->setObjectName(QStringLiteral("pluginMetadata"));
  plugin_metadata_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  plugin_metadata_->setWordWrap(true);
  details_layout->addWidget(plugin_metadata_);
  plugin_description_ = new QLabel(details);
  plugin_description_->setObjectName(QStringLiteral("pluginDescription"));
  plugin_description_->setWordWrap(true);
  plugin_description_->setTextInteractionFlags(Qt::TextSelectableByMouse);
  details_layout->addWidget(plugin_description_);
  plugin_homepage_ = new QLabel(details);
  plugin_homepage_->setTextFormat(Qt::RichText);
  plugin_homepage_->setTextInteractionFlags(Qt::TextBrowserInteraction);
  plugin_homepage_->setOpenExternalLinks(false);
  details_layout->addWidget(plugin_homepage_);
  auto* commands_label = new QLabel(tr("Ribbon commands"), details);
  details_layout->addWidget(commands_label);
  plugin_commands_ = new QListWidget(details);
  plugin_commands_->setObjectName(QStringLiteral("pluginCommandList"));
  plugin_commands_->setSelectionMode(QAbstractItemView::NoSelection);
  plugin_commands_->setIconSize(QSize(24, 24));
  details_layout->addWidget(plugin_commands_, 1);
  plugin_empty_ = new QLabel(tr("No plugins are currently loaded."), details);
  plugin_empty_->setObjectName(QStringLiteral("pluginEmpty"));
  plugin_empty_->setAlignment(Qt::AlignCenter);
  plugin_empty_->setWordWrap(true);
  details_layout->addWidget(plugin_empty_, 1);
  installed_layout->addWidget(details, 1);
  tabs->addTab(installed, tr("Installed"));

  auto* layout_tab = new QWidget(tabs);
  auto* layout_root = new QVBoxLayout(layout_tab);
  layout_root->setContentsMargins(10, 10, 10, 10);
  auto* selectors = new QHBoxLayout();
  selectors->addWidget(new QLabel(tr("Page"), layout_tab));
  page_combo_ = new QComboBox(layout_tab);
  selectors->addWidget(page_combo_, 1);
  selectors->addWidget(new QLabel(tr("Group"), layout_tab));
  group_combo_ = new QComboBox(layout_tab);
  selectors->addWidget(group_combo_, 1);
  layout_root->addLayout(selectors);
  order_list_ = new QListWidget(layout_tab);
  order_list_->setObjectName(QStringLiteral("ribbonOrderList"));
  order_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  order_list_->setIconSize(QSize(28, 28));
  layout_root->addWidget(order_list_, 1);
  auto* order_buttons = new QHBoxLayout();
  order_buttons->addStretch(1);
  move_up_ = new QPushButton(tr("Move Up"), layout_tab);
  move_down_ = new QPushButton(tr("Move Down"), layout_tab);
  order_buttons->addWidget(move_up_);
  order_buttons->addWidget(move_down_);
  layout_root->addLayout(order_buttons);
  tabs->addTab(layout_tab, tr("Ribbon Layout"));

  auto* buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
                           this);
  root->addWidget(buttons);

  populate_plugins();
  populate_locations();
  apply_stylesheet();

  connect(plugin_list_, &QListWidget::currentRowChanged, this,
          &PluginManagerDialog::show_plugin_details);
  connect(page_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { populate_groups(); });
  connect(group_combo_, &QComboBox::currentIndexChanged, this,
          [this](int) { populate_order_list(); });
  connect(move_up_, &QPushButton::clicked, this,
          [this] { move_selected_command(-1); });
  connect(move_down_, &QPushButton::clicked, this,
          [this] { move_selected_command(1); });
  connect(plugin_homepage_, &QLabel::linkActivated, this,
          [](const QString& link) {
            const QUrl url(link);
            if (url.isValid() &&
                (url.scheme() == QStringLiteral("http") ||
                 url.scheme() == QStringLiteral("https"))) {
              QDesktopServices::openUrl(url);
            }
          });
  connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

std::unordered_set<std::string>
PluginManagerDialog::disabled_loaded_ids() const {
  std::unordered_set<std::string> disabled;
  for (int i = 0; i < plugin_list_->count(); ++i) {
    const QListWidgetItem* item = plugin_list_->item(i);
    if (item != nullptr && item->checkState() != Qt::Checked) {
      disabled.insert(item->data(Qt::UserRole).toString().toStdString());
    }
  }
  return disabled;
}

void PluginManagerDialog::populate_plugins() {
  plugin_list_->clear();
  for (const PluginInfo& plugin : manager_->plugins()) {
    auto* item = new QListWidgetItem(plugin_list_);
    item->setText(to_qstring(plugin.title) +
                  (plugin.version.empty()
                       ? QString{}
                       : QStringLiteral("  %1").arg(to_qstring(plugin.version))));
    if (!plugin.icon_path.empty()) {
      item->setIcon(QIcon(to_qstring(plugin.icon_path)));
    }
    item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable |
                   Qt::ItemIsUserCheckable);
    item->setCheckState(manager_->is_enabled(plugin.id) ? Qt::Checked
                                                        : Qt::Unchecked);
    item->setData(Qt::UserRole, to_qstring(plugin.id));
  }
  const bool empty = plugin_list_->count() == 0;
  plugin_list_->setVisible(!empty);
  plugin_empty_->setVisible(empty);
  if (!empty) {
    plugin_list_->setCurrentRow(0);
    show_plugin_details(0);
  }
}

void PluginManagerDialog::show_plugin_details(int row) {
  const QListWidgetItem* item = plugin_list_->item(row);
  const PluginInfo* plugin =
      item == nullptr
          ? nullptr
          : find_plugin(*manager_,
                        item->data(Qt::UserRole).toString().toStdString());
  if (plugin == nullptr) {
    plugin_icon_->clear();
    plugin_title_->clear();
    plugin_badge_->clear();
    plugin_metadata_->clear();
    plugin_description_->clear();
    plugin_homepage_->clear();
    plugin_commands_->clear();
    return;
  }

  QPixmap icon;
  if (!plugin->icon_path.empty()) {
    icon.load(to_qstring(plugin->icon_path));
  }
  plugin_icon_->setPixmap(icon.isNull()
                              ? QPixmap{}
                              : icon.scaled(44, 44, Qt::KeepAspectRatio,
                                            Qt::SmoothTransformation));
  plugin_title_->setText(to_qstring(plugin->title));
  plugin_badge_->setText(plugin->built_in ? tr("Built-in") : QString{});
  plugin_metadata_->setText(
      tr("Author: %1\nVersion: %2\nReleased: %3\nPlugin ID: %4")
          .arg(plugin->author.empty() ? tr("Unknown")
                                      : to_qstring(plugin->author),
               plugin->version.empty() ? tr("Unknown")
                                       : to_qstring(plugin->version),
               plugin->release_date.empty() ? tr("Unknown")
                                            : to_qstring(plugin->release_date),
               to_qstring(plugin->id)));
  plugin_description_->setText(
      plugin->description.empty() ? tr("No description provided.")
                                  : to_qstring(plugin->description));
  if (plugin->homepage_url.empty()) {
    plugin_homepage_->clear();
  } else {
    const QString url = to_qstring(plugin->homepage_url);
    plugin_homepage_->setText(
        QStringLiteral("<a href=\"%1\">%2</a>")
            .arg(url.toHtmlEscaped(), tr("Open plugin homepage")));
  }

  plugin_commands_->clear();
  for (const PluginCommand& command : manager_->commands()) {
    if (command.plugin_id != plugin->id) {
      continue;
    }
    auto* command_item = new QListWidgetItem(plugin_commands_);
    command_item->setText(
        tr("%1    %2 / %3")
            .arg(to_qstring(command.title),
                 location_title(command.placement.page_id),
                 location_title(command.placement.group_id)));
    if (!command.placement.icon_path.empty()) {
      command_item->setIcon(QIcon(to_qstring(command.placement.icon_path)));
    }
    command_item->setToolTip(to_qstring(command.id));
  }
}

void PluginManagerDialog::populate_locations() {
  std::vector<std::string> pages;
  for (const PluginCommand& command : manager_->commands()) {
    if (std::find(pages.begin(), pages.end(), command.placement.page_id) ==
        pages.end()) {
      pages.push_back(command.placement.page_id);
    }
  }
  std::sort(pages.begin(), pages.end());
  page_combo_->clear();
  for (const std::string& page : pages) {
    page_combo_->addItem(location_title(page), to_qstring(page));
  }
  populate_groups();
}

void PluginManagerDialog::populate_groups() {
  const std::string page = page_combo_->currentData().toString().toStdString();
  std::vector<std::string> groups;
  for (const PluginCommand& command : manager_->commands()) {
    if (command.placement.page_id == page &&
        std::find(groups.begin(), groups.end(), command.placement.group_id) ==
            groups.end()) {
      groups.push_back(command.placement.group_id);
    }
  }
  std::sort(groups.begin(), groups.end());
  group_combo_->clear();
  for (const std::string& group : groups) {
    group_combo_->addItem(location_title(group), to_qstring(group));
  }
  populate_order_list();
}

void PluginManagerDialog::populate_order_list() {
  const std::string page = page_combo_->currentData().toString().toStdString();
  const std::string group =
      group_combo_->currentData().toString().toStdString();
  std::vector<const PluginCommand*> commands;
  for (const PluginCommand& command : manager_->commands()) {
    if (command.placement.page_id == page &&
        command.placement.group_id == group) {
      commands.push_back(&command);
    }
  }
  std::unordered_map<std::string, std::size_t> rank;
  for (std::size_t i = 0; i < working_order_.size(); ++i) {
    rank.emplace(working_order_[i], i);
  }
  std::stable_sort(commands.begin(), commands.end(),
                   [&rank](const PluginCommand* a, const PluginCommand* b) {
                     const auto ar = rank.find(a->id);
                     const auto br = rank.find(b->id);
                     if (ar != rank.end() || br != rank.end()) {
                       if (ar == rank.end()) return false;
                       if (br == rank.end()) return true;
                       return ar->second < br->second;
                     }
                     if (a->placement.order != b->placement.order) {
                       return a->placement.order < b->placement.order;
                     }
                     return a->id < b->id;
                   });
  for (const PluginCommand* command : commands) {
    if (std::find(working_order_.begin(), working_order_.end(), command->id) ==
        working_order_.end()) {
      working_order_.push_back(command->id);
    }
  }

  order_list_->clear();
  for (const PluginCommand* command : commands) {
    const PluginInfo* plugin = find_plugin(*manager_, command->plugin_id);
    auto* item = new QListWidgetItem(order_list_);
    item->setText(
        tr("%1    — %2")
            .arg(to_qstring(command->title),
                 plugin == nullptr ? to_qstring(command->plugin_id)
                                   : to_qstring(plugin->title)));
    item->setData(Qt::UserRole, to_qstring(command->id));
    if (!command->placement.icon_path.empty()) {
      item->setIcon(QIcon(to_qstring(command->placement.icon_path)));
    }
  }
  if (order_list_->count() > 0) {
    order_list_->setCurrentRow(0);
  }
  move_up_->setEnabled(order_list_->count() > 1);
  move_down_->setEnabled(order_list_->count() > 1);
}

void PluginManagerDialog::move_selected_command(int delta) {
  const int row = order_list_->currentRow();
  const int target = row + delta;
  if (row < 0 || target < 0 || target >= order_list_->count()) {
    return;
  }
  const std::string id =
      order_list_->item(row)->data(Qt::UserRole).toString().toStdString();
  const std::string other =
      order_list_->item(target)->data(Qt::UserRole).toString().toStdString();
  auto a = std::find(working_order_.begin(), working_order_.end(), id);
  auto b = std::find(working_order_.begin(), working_order_.end(), other);
  if (a == working_order_.end() || b == working_order_.end()) {
    return;
  }
  std::iter_swap(a, b);
  populate_order_list();
  order_list_->setCurrentRow(target);
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
      (event->type() == QEvent::ThemeChange ||
       event->type() == QEvent::PaletteChange)) {
    apply_stylesheet();
  }
  QDialog::changeEvent(event);
}

}  // namespace tamias
