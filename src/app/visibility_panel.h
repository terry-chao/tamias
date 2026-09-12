#pragma once

#include "entity/entity.h"

#include <QWidget>
#include <unordered_map>
#include <utility>
#include <vector>

class QLabel;
class QLineEdit;
class QPoint;
class QStackedWidget;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace tamias {

class DocumentViewport;

// 右侧「构件」面板：按专业分组的一棵构件树，每行一个复选框，勾选 = 显示。
// 面板不保存过滤状态——状态始终在视口里（hidden_kinds_ / hidden_ids_），
// 勾选写回视口，视口的 visibility_changed 再回灌刷新，两边不会各记一份。
class VisibilityPanel final : public QWidget {
  Q_OBJECT
 public:
  explicit VisibilityPanel(QWidget* parent = nullptr);

  // 卡片式宿主（视口工具面板）本身是深色，嵌进去时固定用深色配色。
  void set_dark_theme(bool dark);
  // 绑定当前文档视口；传 nullptr 表示当前页没有模型（起始页 / 图纸页）。
  void set_viewport(DocumentViewport* viewport);
  void refresh();

 protected:
  void showEvent(QShowEvent* event) override;

 private:
  enum class RowType : int { Group = 0, Kind = 1, Imported = 2 };

  void build_tree();
  void sync_rows();
  void apply_search(const QString& text);
  void update_groups();
  static RowType row_type(const QTreeWidgetItem* item);
  static bool kind_of(const QTreeWidgetItem* item, EntityKind& out);
  void on_item_changed(QTreeWidgetItem* item, int column);
  void on_item_double_clicked(QTreeWidgetItem* item, int column);
  void show_context_menu(const QPoint& pos);
  void set_row_hidden(QTreeWidgetItem* item, bool hidden_row);

  DocumentViewport* viewport_ = nullptr;
  QLineEdit* search_ = nullptr;
  QToolButton* show_all_ = nullptr;
  QStackedWidget* pages_ = nullptr;
  QTreeWidget* tree_ = nullptr;
  QLabel* hint_ = nullptr;
  std::unordered_map<EntityKind, QTreeWidgetItem*> kind_items_;
  std::vector<std::pair<Discipline, QTreeWidgetItem*>> group_items_;
  QTreeWidgetItem* imported_item_ = nullptr;
  bool syncing_ = false;
  bool dark_ = false;
  std::vector<QMetaObject::Connection> viewport_connections_;
};

}  // namespace tamias
