#pragma once

#include "engine/modeling/nodes/node_type.h"
#include "engine/modeling/nodes/node_value.h"

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;

namespace tamias {

// 节点搜索面板：QLineEdit + 列表，模糊搜索 + 端口兼容度过滤。
// 从端口拖到空白画布时带端口过滤（并自动连最佳端口，连线的逻辑在 NodeCanvas）。
class NodePalettePopup final : public QWidget {
  Q_OBJECT
 public:
  explicit NodePalettePopup(QWidget* parent = nullptr);

  // 在全局坐标弹出。filter_port != Empty 时按「新节点要有能接 / 能出这个类型的
  // 端口」排序（search_for_port），否则普通搜索。
  void spawn(const QPoint& global_pos, PortType filter_port, bool as_input);

 signals:
  void node_chosen(const tamias::NodeTypeInfo* type);
  void cancelled();

 private:
  void refresh_list();
  void choose_current();

  QLineEdit* edit_ = nullptr;
  QListWidget* list_ = nullptr;
  PortType filter_port_ = PortType::Empty;
  bool as_input_ = false;
  bool chosen_ = false;  // 本次弹出是否已经选过（防止双击触发两次）
};

}  // namespace tamias
