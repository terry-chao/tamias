#pragma once

#include <QPoint>
#include <QString>
#include <QWidget>
#include <vector>

class QAction;
class QEvent;
class QFrame;
class QHBoxLayout;
class QLabel;
class QMouseEvent;
class QPaintEvent;
class QToolButton;
class QVBoxLayout;

namespace tamias {

// Ribbon 的两种形态：图标 + 文字（默认），或只有图标、悬浮出提示（FreeCAD 那种）。
enum class RibbonDisplayMode { IconWithText, IconOnly };

// 拖动分组用的私有 MIME 类型，payload 是 "page_id|group_id"。
inline constexpr char kRibbonGroupMimeType[] = "application/x-tamias-ribbon-group";

// 分组顶上的抓手条：拖它可以把整组工具拖出 Ribbon（SketchUp 那种浮动小工具栏），
// 拖回 Ribbon 再按落点停靠回去。
class RibbonGrip final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonGrip(QWidget* parent = nullptr);

 protected:
  void paintEvent(QPaintEvent* event) override;
};

class RibbonGroup final : public QWidget {
  Q_OBJECT
 public:
  explicit RibbonGroup(const QString& title, QWidget* parent = nullptr);

  QToolButton* add_action(QAction* action);
  void reorder_buttons(const std::vector<QToolButton*>& ordered);
  void set_separator_visible(bool visible);

  void set_display_mode(RibbonDisplayMode mode);
  void set_identity(const QString& page_id, const QString& group_id);
  void set_floating(bool floating);

  // 以 source 为拖拽源把这一组拖起来：分组自己的抓手上走这条，浮动窗的标题栏也走这条。
  void begin_drag(QWidget* source, const QPoint& press_pos_in_source);

  [[nodiscard]] QString title() const { return title_text_; }
  [[nodiscard]] QString page_id() const { return page_id_; }
  [[nodiscard]] QString group_id() const { return group_id_; }
  [[nodiscard]] bool floating() const { return floating_; }

 signals:
  // 拖完没落在 Ribbon 上：由窗口决定「浮起来」还是「把已有的浮动窗挪过去」。
  void drag_dropped_outside(RibbonGroup* group, const QPoint& group_top_left);

 protected:
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void apply_display_mode();
  [[nodiscard]] std::vector<QToolButton*> buttons() const;

  RibbonDisplayMode mode_ = RibbonDisplayMode::IconWithText;
  QString title_text_;
  QString page_id_;
  QString group_id_;
  QHBoxLayout* buttons_layout_ = nullptr;
  RibbonGrip* grip_ = nullptr;
  QWidget* buttons_host_ = nullptr;
  QLabel* title_ = nullptr;
  QFrame* separator_ = nullptr;
  bool floating_ = false;
  bool armed_ = false;
  QPoint press_pos_in_group_;
};

}  // namespace tamias
