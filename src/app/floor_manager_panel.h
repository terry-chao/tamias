#pragma once

#include <QWidget>

#include <vector>

class QLabel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace tamias {

class DocumentViewport;

// 右侧工具列里的「楼层管理」页：把楼层当成一张**视图清单**用——第一行是全局三维
// （默认打开的就是它），下面一层一行；双击某行就打开那个视图。
//
// 面板自己不记状态：当前打开的是哪个视图由视口说了算（见 DocumentViewport 的
// floor_view_），双击调回视口，视口的 view_changed / visibility_changed 再回灌刷新。
// 和「楼层」页的分工：那一页管显隐与楼层表，这一页只管"打开哪张视图"。
class FloorManagerPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit FloorManagerPanel(QWidget* parent = nullptr);

  // 卡片式宿主（视口工具面板）本身跟着系统深浅色，嵌进去时同步一次。
  void set_dark_theme(bool dark);
  void set_viewport(DocumentViewport* viewport);
  void refresh();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void sync_rows();
  void on_item_double_clicked(QTreeWidgetItem* item, int column);
  void open_view_for(QTreeWidgetItem* item);

  DocumentViewport* viewport_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* hint_ = nullptr;
  bool syncing_ = false;
  bool dark_ = false;
  std::vector<QMetaObject::Connection> viewport_connections_;
};

}  // namespace tamias
