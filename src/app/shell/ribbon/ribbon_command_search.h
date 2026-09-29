#pragma once

#include "app/shell/ribbon/ribbon_command_entry.h"

#include <QLineEdit>
#include <QString>
#include <functional>
#include <vector>

class QAbstractItemView;
class QCompleter;
class QModelIndex;
class QStandardItemModel;

namespace tamias {

// 菜单行右端的命令搜索框。
//
// 起因：工具带上的按钮越来越多，切成「仅图标」之后一排小图认不出谁是谁。这里按名字
// 搜（也认出处、悬浮提示和快捷键），命中的命令就列在下面，回车或单击直接执行。
//
// 清单是**现取**的（EntryProvider 每次输入都调一遍）：插件命令随扩展重载重建，
// 缓存一份就会搜到已经不在的按钮。
class RibbonCommandSearch final : public QLineEdit {
  Q_OBJECT
 public:
  explicit RibbonCommandSearch(QWidget* parent = nullptr);

  using EntryProvider = std::function<std::vector<RibbonCommandEntry>()>;
  void set_entry_provider(EntryProvider provider);

  // 结果列表是一个独立的顶层窗口（Qt::Popup），宿主的样式表不会自己传下去，
  // 换了主题要由外面把同一份样式表发过来。
  void set_popup_stylesheet(const QString& sheet);

 private:
  void refresh_results();
  void activate_index(const QModelIndex& index);

  EntryProvider provider_;
  QStandardItemModel* model_ = nullptr;
  QCompleter* completer_ = nullptr;
  QAbstractItemView* popup_ = nullptr;
};

}  // namespace tamias
