#pragma once

#include "engine/modeling/nodes/node_graph.h"
#include "engine/modeling/nodes/node_type.h"
#include "engine/modeling/nodes/node_value.h"
#include "host/session.h"

#include <QWidget>

#include <map>
#include <set>

class QLineEdit;
class QMenu;
class QPainter;

namespace tamias {

class NodePalettePopup;

// 节点画布：全 QPainter 自绘（模板 drawing_view 的 pan / zoom-to-cursor 思路）。
// 只读 Session / Document，结构编辑全部经命令（graph_edit_commands）走撤销栈；
// 拖拽期间直接改节点 x/y 或值（预览手感），松手时补一条「起点 → 终点」命令。
class NodeCanvas final : public QWidget {
  Q_OBJECT
 public:
  explicit NodeCanvas(QWidget* parent = nullptr);

  void set_session(Session* session);
  void frame_all();
  // 在画布中央弹搜索面板（面板工具栏的「添加节点」按钮用）。
  void open_palette_centered();

 signals:
  // 任何会改变求值结果的编辑（结构 / 值）。面板据此防抖求值。
  void graph_edited();
  // 只改了显示状态（选中 / 预览眼睛 / 位置）——面板刷新状态栏即可。
  void graph_touched();
  void status_message(const QString& text);
  void selection_changed();

 protected:
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  bool eventFilter(QObject* watched, QEvent* event) override;

 private:
  // —— 命中测试 ——
  struct Hit {
    enum class Kind { None, Header, Body, InPort, OutPort, InField, Param, Eye };
    Kind kind = Kind::None;
    NodeId node = kInvalidNodeId;
    int index = 0;
  };

  // 卡片布局（世界坐标，随节点 x/y 平移）。
  struct CardLayout {
    double height = 90.0;
    std::vector<double> out_ys;   // 输出端口圆心 y
    std::vector<double> in_ys;    // 输入端口圆心 y
    std::vector<QRectF> param_rects;
    std::vector<QRectF> input_rects;  // 未连线端口的内联值字段
    QPointF eye_center;
    double eye_radius = 7.0;
  };

  [[nodiscard]] Graph* graph();
  [[nodiscard]] const Graph* graph() const;
  [[nodiscard]] Document* document();
  [[nodiscard]] CardLayout layout_for(const Graph::Node& node) const;
  // 新节点的落点：压到别的卡片就往右下让，别叠成一张。
  [[nodiscard]] QPointF free_node_spot(const NodeTypeInfo* type, const QPointF& desired) const;
  [[nodiscard]] Hit hit_test(const QPointF& device_pos) const;
  // Alt+点击删线用：找光标下方的连线（按采样点距离，阈值随缩放）。
  [[nodiscard]] const Link* hit_link_at(const QPointF& device_pos) const;
  [[nodiscard]] QPointF device_to_world(const QPointF& device) const;
  [[nodiscard]] QPointF world_to_device(const QPointF& world) const;
  // 空画布提示：一行标题 + 一行说明（居中，随宽度省略）。
  void draw_empty_state(QPainter& painter, const QString& title,
                        const QString& hint) const;

  // 执行命令并广播 graph_edited；失败弹 toast。
  template <typename CommandT, typename... Args>
  bool run_command(Args&&... args);

  void spawn_palette(const QPoint& global_pos);
  void finish_palette_add(const NodeTypeInfo* type);
  void open_choice_menu(const Graph::Node& node, int param_index, const QPoint& global_pos);
  void open_inline_editor(const Hit& hit);
  void commit_inline_editor();
  void cancel_inline_editor();
  void toggle_preview_flag(NodeId node);
  void delete_selected();
  void apply_hover_cursor(const QPointF& device_pos);

  // 拖拽状态机。
  enum class Drag { None, Pan, Nodes, Box, Wire, Value };
  Drag drag_ = Drag::None;
  QPointF drag_start_device_;
  QPointF pan_start_offset_;
  bool right_button_down_ = false;
  bool right_moved_ = false;
  std::map<NodeId, std::pair<double, double>> move_origins_;
  QRectF box_rect_device_;

  // 拖线：从哪个端口拖出。from_output = 从输出端口拖（找输入口接），
  // 否则从输入端口拖（找输出口接）。
  struct PendingWire {
    bool active = false;
    bool from_output = false;
    NodeId node = kInvalidNodeId;
    int port = 0;
    QPointF world_pos;  // 拖出端口的世界坐标（画拖线用）
  } wire_;
  QPointF wire_cursor_world_;

  // 拖值（scrub）。
  bool scrub_param_ = false;  // false = 内联输入值，true = 参数
  NodeId scrub_node_ = kInvalidNodeId;
  int scrub_index_ = 0;
  NodeValue scrub_start_;

  Session* session_ = nullptr;
  NodePalettePopup* palette_ = nullptr;
  QLineEdit* inline_editor_ = nullptr;
  Hit inline_editor_target_;
  QPointF palette_drop_world_;  // 搜索面板弹出处的世界坐标（新节点落这里）

  std::set<NodeId> selection_;
  Hit hover_;
  QPointF offset_;  // 世界原点在设备坐标上的位置
  double zoom_ = 1.0;
};

}  // namespace tamias
