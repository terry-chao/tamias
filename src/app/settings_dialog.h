#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QEvent;
class QLabel;
class QListWidget;

namespace tamias {

class SettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

  [[nodiscard]] bool language_changed() const { return language_changed_; }
  [[nodiscard]] bool backend_changed() const { return backend_changed_; }
  [[nodiscard]] bool theme_changed() const { return theme_changed_; }

 protected:
  void changeEvent(QEvent* event) override;

 private slots:
  void accept() override;
  void on_backend_changed(int index);

 private:
  void apply_stylesheet();
  void populate_categories();

  QListWidget* nav_ = nullptr;
  QComboBox* backend_combo_ = nullptr;
  QComboBox* language_combo_ = nullptr;
  QComboBox* theme_combo_ = nullptr;
  QCheckBox* zoom_to_mouse_check_ = nullptr;
  QLabel* backend_hint_ = nullptr;
  bool language_changed_ = false;
  bool backend_changed_ = false;
  bool theme_changed_ = false;
  bool applying_stylesheet_ = false;
};

}  // namespace tamias
