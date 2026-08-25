#pragma once

#include <QDialog>

#include <unordered_set>
#include <string>

class QEvent;
class QLabel;
class QListWidget;

namespace tamias {

class PluginManager;

class PluginManagerDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit PluginManagerDialog(PluginManager& manager, QWidget* parent = nullptr);

  [[nodiscard]] std::unordered_set<std::string> hidden_loaded_ids() const;

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_stylesheet();
  void populate_list();

  PluginManager* manager_ = nullptr;
  QListWidget* list_ = nullptr;
  QLabel* hint_ = nullptr;
  QLabel* empty_ = nullptr;
  bool applying_stylesheet_ = false;
};

}  // namespace tamias
