#pragma once

#include <QFrame>
#include <QString>

class QFormLayout;
class QPushButton;
class QWidget;

namespace tamias {

class SettingsSection final : public QFrame {
  Q_OBJECT
 public:
  explicit SettingsSection(const QString& title, QWidget* parent = nullptr);

  void add_row(const QString& label, QWidget* field);
  void add_row(QWidget* widget);

 private slots:
  void on_toggled(bool expanded);

 private:
  void set_header_text(bool expanded);

  QPushButton* header_ = nullptr;
  QWidget* body_ = nullptr;
  QFormLayout* form_ = nullptr;
  QString title_;
};

}  // namespace tamias
