#pragma once

#include <QWidget>

#include <cstdint>
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
// **一件事一个入口**：行首勾选框只管显隐；点行本身 = 让主窗口开（或切到）这一层的
// 独立页签——那张页签里只有这一层，本层底面就是原点（见 DocumentViewport::
// enter_floor_workspace）；第一行「全局三维」点它回到文档页签。显隐与"看哪一层"互不越权。
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

 signals:
  // 点某一行：0 = 第一行「全局三维」（回文档页签），其余是那一层的 storey id
  // （开 / 切那一层的页签）。面板自己开不了页签，交给主窗口。
  void floor_view_requested(std::uint64_t storey_id);

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void sync_rows();
  void sync_storey_combo();
  void on_item_changed(QTreeWidgetItem* item, int column);
  void on_item_clicked(QTreeWidgetItem* item, int column);
  // 点某行：第 0 行（全局三维）回文档页签，其余开 / 切对应楼层的页签。
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
