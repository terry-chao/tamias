#pragma once

#include <QDialog>

class QEvent;

namespace tamias {

class AboutDialog final : public QDialog {
  Q_OBJECT
 public:
  explicit AboutDialog(QWidget* parent = nullptr);

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_stylesheet();

  bool applying_stylesheet_ = false;
};

}  // namespace tamias
