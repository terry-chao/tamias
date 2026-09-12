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

// 右侧工具列里的「楼层」页：一层一行，勾选 = 显示；行里带标高与层高。
// 面板自己不记状态——显隐真相在视口里（hidden_floors_），当前楼层在文档里
// （bim().active_storey_id()），勾选/切换都写回去，再从视口的信号回灌刷新。
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
  void on_storey_combo_changed(int index);
  void open_floor_settings();

  DocumentViewport* viewport_ = nullptr;
  QComboBox* storey_combo_ = nullptr;
  QToolButton* settings_ = nullptr;
  QToolButton* show_all_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* hint_ = nullptr;
  bool syncing_ = false;
  bool dark_ = false;
  std::vector<QMetaObject::Connection> viewport_connections_;
};

}  // namespace tamias
