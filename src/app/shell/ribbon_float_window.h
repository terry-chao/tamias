#pragma once

#include <QPoint>
#include <QWidget>

class QEvent;
class QCloseEvent;
class QLabel;
class QMoveEvent;
class QMouseEvent;
class QToolButton;
class QVBoxLayout;

namespace tamias {

class RibbonGroup;

// 从 Ribbon 里拖出来的一组工具：一个没有系统边框的小窗。标题栏可以拖着走，
// 拖回 Ribbon 就停靠回去（SketchUp 的浮动工具栏），× 按钮直接收回。
class RibbonFloatWindow final : public QWidget {
  Q_OBJECT
 public:
  RibbonFloatWindow(RibbonGroup* group, QWidget* parent);

  // 把分组从浮窗里摘下来（交给调用方重新停靠），浮窗自己不再管它。
  void release_group();
  [[nodiscard]] QPoint group_offset() const;

 signals:
  void dock_requested(RibbonGroup* group);
  // 浮窗被拖到别的地方了。拖动过程中会连发很多次，接收方自己节流再存盘。
  void moved();

 protected:
  void changeEvent(QEvent* event) override;
  void closeEvent(QCloseEvent* event) override;
  void moveEvent(QMoveEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;

 private:
  void apply_theme();

  RibbonGroup* group_ = nullptr;
  QVBoxLayout* root_ = nullptr;
  QWidget* header_ = nullptr;
  QLabel* title_ = nullptr;
  QToolButton* dock_button_ = nullptr;
  bool armed_ = false;
  QPoint press_pos_;
  // setStyleSheet 会回头送 PaletteChange，不加这个标志就会自己递归到爆栈。
  bool applying_theme_ = false;
};

}  // namespace tamias
