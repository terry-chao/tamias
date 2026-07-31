#pragma once

#include <QDialog>

class QComboBox;
class QLabel;

namespace tamias {

class SettingsDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit SettingsDialog(QWidget* parent = nullptr);

 private slots:
  void accept() override;
  void on_backend_changed(int index);

 private:
  QComboBox* backend_combo_ = nullptr;
  QLabel* backend_hint_ = nullptr;
};

}  // namespace tamias
