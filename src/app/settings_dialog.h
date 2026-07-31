#pragma once

#include <QDialog>

class QComboBox;
class QLabel;

namespace tamias {

class SettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

  [[nodiscard]] bool language_changed() const { return language_changed_; }

 private slots:
  void accept() override;
  void on_backend_changed(int index);

 private:
  QComboBox* backend_combo_ = nullptr;
  QComboBox* language_combo_ = nullptr;
  QLabel* backend_hint_ = nullptr;
  bool language_changed_ = false;
};

}  // namespace tamias
