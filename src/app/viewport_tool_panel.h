#pragma once

#include <QIcon>
#include <QWidget>

#include "theme.h"

class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace tamias {

class DocumentViewport;
class FloorPanel;
class VisibilityPanel;

// 视口右侧的一整列工具面板（不是浮层）：视口在它左边，它从上到下占满视口高度，
// 再往右才是停靠面板区。列内左侧一排按钮（2D/3D、构件显隐、楼层、适应窗口），
// 点开带功能页的按钮后，右侧展开对应页面（构件显隐树、楼层表）；收起时整列只有按钮宽。
class ViewportToolPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit ViewportToolPanel(QWidget* parent = nullptr);

  // 绑定当前文档视口（可见性页读写的都是它）。
  void set_viewport(DocumentViewport* viewport);
  void set_plan_view(bool plan);

  [[nodiscard]] bool panel_open() const { return active_page_ >= 0; }
  [[nodiscard]] int preferred_width() const;
  void toggle_visibility_page();
  void toggle_floor_page();

 signals:
  void plan_view_toggled(bool plan);
  void frame_all_clicked();
  void layout_changed();  // 展开/收起功能页后，视口要重新摆叠加层（ViewCube 等）

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  enum Page { kVisibilityPage = 0, kFloorPage = 1 };

  void set_active_page(int page);
  void apply_width();
  QToolButton* add_rail_button(QVBoxLayout* layout, const QIcon& icon, const QString& tip);
  [[nodiscard]] QIcon load_icon(const QString& resource) const;

  QWidget* rail_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  VisibilityPanel* visibility_page_ = nullptr;
  FloorPanel* floor_page_ = nullptr;
  QToolButton* plan_button_ = nullptr;
  QToolButton* visibility_button_ = nullptr;
  QToolButton* floor_button_ = nullptr;
  ThemePalette theme_;
  QIcon icon_2d_;
  QIcon icon_3d_;
  int active_page_ = -1;
};

}  // namespace tamias
