#pragma once

#include <QWidget>

#include <vector>

class QLabel;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace tamias {

class DocumentViewport;

// 视口右侧工具列里的「图纸管理」页（和「构件显隐 / 楼层 / 楼层管理」同一列）：
// 列出**当前文档挂着的参考图纸**，添加 / 删除，双击一行把图纸开成二维页签查看。
// 只存路径——真相在 Document::drawing_paths()，随 .tdoc 一起存；图纸内容现用现读，
// 不把图纸并进文档。面板自己不记状态：增删都写回视口，再用视口信号回灌刷新。
class DrawingManagerPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit DrawingManagerPanel(QWidget* parent = nullptr);

  // 卡片式宿主（视口工具面板）本身跟着系统深浅色，嵌进去时同步一次。
  void set_dark_theme(bool dark);
  // 绑定当前文档视口；传 nullptr（起始页 / 图纸页）显示提示。
  void set_viewport(DocumentViewport* viewport);
  void refresh();

 signals:
  void open_requested(const QString& path);  // 双击 / 「打开」：请主窗口开成二维页签

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  void add_drawings();
  void open_selected();
  void remove_selected();
  void on_item_double_clicked(QTreeWidgetItem* item, int column);
  [[nodiscard]] QString selected_path() const;
  void sync_rows();
  void sync_buttons();

  DocumentViewport* viewport_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* hint_ = nullptr;
  QToolButton* add_button_ = nullptr;
  QToolButton* open_button_ = nullptr;
  QToolButton* remove_button_ = nullptr;
  bool syncing_ = false;
  bool dark_ = false;
  std::vector<QMetaObject::Connection> viewport_connections_;
};

}  // namespace tamias
