#pragma once

#include <QDialog>

#include <string>
#include <unordered_set>
#include <vector>

class QComboBox;
class QEvent;
class QLabel;
class QListWidget;
class QPushButton;

namespace tamias {

class PluginManager;

class PluginManagerDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit PluginManagerDialog(PluginManager& manager,
                               QWidget* parent = nullptr);

  [[nodiscard]] std::unordered_set<std::string> disabled_loaded_ids() const;
  [[nodiscard]] std::vector<std::string> ordered_command_ids() const {
    return working_order_;
  }

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_stylesheet();
  void populate_plugins();
  void show_plugin_details(int row);
  void populate_locations();
  void populate_groups();
  void populate_order_list();
  void move_selected_command(int delta);

  PluginManager* manager_ = nullptr;
  QListWidget* plugin_list_ = nullptr;
  QLabel* plugin_icon_ = nullptr;
  QLabel* plugin_title_ = nullptr;
  QLabel* plugin_badge_ = nullptr;
  QLabel* plugin_metadata_ = nullptr;
  QLabel* plugin_description_ = nullptr;
  QLabel* plugin_homepage_ = nullptr;
  QListWidget* plugin_commands_ = nullptr;
  QLabel* plugin_empty_ = nullptr;
  QComboBox* page_combo_ = nullptr;
  QComboBox* group_combo_ = nullptr;
  QListWidget* order_list_ = nullptr;
  QPushButton* move_up_ = nullptr;
  QPushButton* move_down_ = nullptr;
  std::vector<std::string> working_order_;
  bool applying_stylesheet_ = false;
};

}  // namespace tamias
