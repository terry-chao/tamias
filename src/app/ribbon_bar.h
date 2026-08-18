#pragma once

#include <QWidget>

class QAction;
class QButtonGroup;
class QEvent;
class QHBoxLayout;
class QLabel;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace tamias {

class RibbonPage;

class RibbonBar final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonBar(QWidget* parent = nullptr);

  void add_quick_action(QAction* action);
  RibbonPage* add_page(const QString& title);
  void set_collapsed(bool collapsed);
  [[nodiscard]] bool is_collapsed() const { return collapsed_; }

 protected:
  void changeEvent(QEvent* event) override;

 private:
  void apply_theme();
  void toggle_collapsed();
  void update_collapse_button();

  QWidget* tab_row_ = nullptr;
  QHBoxLayout* quick_layout_ = nullptr;
  QHBoxLayout* tab_buttons_layout_ = nullptr;
  QButtonGroup* tab_group_ = nullptr;
  QToolButton* collapse_button_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  bool collapsed_ = false;
  bool applying_theme_ = false;
};

}  // namespace tamias
