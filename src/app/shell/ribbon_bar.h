#pragma once

#include "app/shell/ribbon_group.h"
#include "app/shell/ribbon_page.h"

#include <QHash>
#include <QIcon>
#include <QPoint>
#include <QString>
#include <QStringList>
#include <QWidget>

class QAction;
class QActionGroup;
class QButtonGroup;
class QEvent;
class QDragEnterEvent;
class QDragLeaveEvent;
class QDragMoveEvent;
class QDropEvent;
class QHBoxLayout;
class QLabel;
class QMenu;
class QMimeData;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace tamias {

class RibbonFloatWindow;
class RibbonPage;

class RibbonBar final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonBar(QWidget* parent = nullptr);

  void add_quick_action(QAction* action);
  RibbonPage* add_page(const QString& title);
  RibbonPage* add_page(const QString& id, const QString& title);
  [[nodiscard]] RibbonPage* find_page(const QString& id) const;
  void set_collapsed(bool collapsed);
  [[nodiscard]] bool is_collapsed() const { return collapsed_; }

  // ==== 两种 Ribbon 形态 ====
  // 图标 + 文字（默认，现状）/ 仅图标（FreeCAD 那种：只有图标，悬浮出提示）。
  void set_display_mode(RibbonDisplayMode mode);
  [[nodiscard]] RibbonDisplayMode display_mode() const { return display_mode_; }
  // 切换按钮的图标由外面给（主题色的 SVG 在 main_window 里统一处理）。
  void set_style_button_icon(const QIcon& icon);

  // ==== 被拖出 Ribbon 的分组（浮动小工具栏）====
  // 每条是 "page_id|group_id|x|y"，用来跨会话记住用户摆的位置。
  [[nodiscard]] QStringList floating_group_keys() const;
  bool restore_floating_group(const QString& page_id, const QString& group_id,
                              const QPoint& window_pos);

 signals:
  void display_mode_changed(RibbonDisplayMode mode);
  void floating_groups_changed();

 protected:
  void changeEvent(QEvent* event) override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dragLeaveEvent(QDragLeaveEvent* event) override;
  void dropEvent(QDropEvent* event) override;

 private:
  struct FloatingEntry {
    RibbonFloatWindow* window = nullptr;
    RibbonPage* page = nullptr;
    RibbonPage::Slot slot;  // 它原来在哪一格，点「收回」时回原位
  };

  void apply_theme();
  void toggle_collapsed();
  void update_collapse_button();
  void build_style_menu();
  void update_style_actions();
  void install_group_hooks(RibbonGroup* group);

  [[nodiscard]] RibbonPage* current_page() const;
  [[nodiscard]] RibbonPage* page_of_group(RibbonGroup* group) const;
  [[nodiscard]] RibbonGroup* group_for_mime(const QMimeData* mime) const;
  [[nodiscard]] bool over_ribbon(const QPoint& global_pos) const;
  // 把分组从页面上摘下来交给一个新的浮动窗（还没定位、还没显示）。
  RibbonFloatWindow* float_group(RibbonGroup* group, RibbonPage* page);
  void dock_group(RibbonGroup* group, RibbonPage* page, RibbonPage::Slot slot);
  void handle_group_dropped_outside(RibbonGroup* group, const QPoint& group_top_left);
  void hide_drop_indicator();
  static void clamp_to_screen(RibbonFloatWindow* window);

  QWidget* tab_row_ = nullptr;
  QHBoxLayout* quick_layout_ = nullptr;
  QHBoxLayout* tab_buttons_layout_ = nullptr;
  QButtonGroup* tab_group_ = nullptr;
  QToolButton* style_button_ = nullptr;
  QMenu* style_menu_ = nullptr;
  QActionGroup* style_group_ = nullptr;
  QAction* style_text_action_ = nullptr;
  QAction* style_icons_action_ = nullptr;
  QToolButton* collapse_button_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QHash<QString, RibbonPage*> pages_by_id_;
  QHash<RibbonGroup*, FloatingEntry> floating_;
  RibbonDisplayMode display_mode_ = RibbonDisplayMode::IconWithText;
  bool collapsed_ = false;
  bool applying_theme_ = false;
};

}  // namespace tamias
