#pragma once

#include <QWidget>
#include <vector>

class QAction;
class QFrame;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace tamias {

class RibbonGroup final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonGroup(const QString& title, QWidget* parent = nullptr);

  QToolButton* add_action(QAction* action);
  void reorder_buttons(const std::vector<QToolButton*>& ordered);
  void set_separator_visible(bool visible);

 private:
  QHBoxLayout* buttons_layout_ = nullptr;
  QLabel* title_ = nullptr;
  QFrame* separator_ = nullptr;
};

}  // namespace tamias
