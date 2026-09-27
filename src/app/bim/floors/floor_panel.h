#pragma once

#include <QWidget>

#include <vector>

class QComboBox;
class QLabel;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace tamias {

class DocumentViewport;

// 右侧工具列里的「楼层」页：把楼层列成一张清单——一层一行，行里带标高与层高。
// **一件事一个入口**：行首勾选框只管显隐；点行本身 = 打开这一层的视图（顺手设为
// 当前楼层）；第一行「全局三维」点它回到全局视图。显隐与"看哪一层"互不越权。
// 面板自己不记状态——显隐真相在视口里（hidden_floors_），打开的视图在视口里
// （floor_view_），当前楼层在文档里（bim().active_storey_id()）；操作都写回视口，
// 再从视口的信号回灌刷新。
// 「楼层设置」开的是独立对话框，改的是楼层表本身（可撤销）。
class FloorPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit FloorPanel(QWidget* parent = nullptr);

  // 卡片式宿主（视口工具面板）本身跟着系统深浅色，嵌进去时同步一次。
  void set_dark_theme(bool dark);
  void set_viewport(DocumentViewport* viewport);
  void refresh();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void sync_rows();
  void sync_storey_combo();
  void on_item_changed(QTreeWidgetItem* item, int column);
  void on_item_clicked(QTreeWidgetItem* item, int column);
  // 点某行：第 0 行（全局三维）打开全局视图，其余打开对应楼层的视图。
  void open_view_for(QTreeWidgetItem* item);
  // 这一下点的是不是行首的勾选框？（勾选框只管显隐，不该顺手切视图。）
  [[nodiscard]] bool click_hit_check_indicator(const QTreeWidgetItem* item) const;
  void on_storey_combo_changed(int index);
  void open_floor_settings();
  void open_copy_floor();

  DocumentViewport* viewport_ = nullptr;
  QComboBox* storey_combo_ = nullptr;
  QToolButton* settings_ = nullptr;
  QToolButton* copy_ = nullptr;
  QToolButton* show_all_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* hint_ = nullptr;
  bool syncing_ = false;
  bool dark_ = false;
  std::vector<QMetaObject::Connection> viewport_connections_;
};

}  // namespace tamias
