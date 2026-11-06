#pragma once

#include "engine/modeling/nodes/node_value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace tamias {

struct NodeTypeInfo;

using NodeId = std::uint64_t;
inline constexpr NodeId kInvalidNodeId = 0;

// 一根连线：纯值类型。from = 上游节点的输出端口，to = 下游节点的输入端口。
struct Link {
  NodeId from_node = kInvalidNodeId;
  std::uint16_t from_port = 0;
  NodeId to_node = kInvalidNodeId;
  std::uint16_t to_port = 0;

  bool operator==(const Link&) const = default;
};

enum class NodeState : std::uint8_t { Ok = 0, Warning, Error };

// 节点图（每个文档一张，见 Document::node_graph()）。
//
// 设计约定（对齐 MiniCAD src/NodeGraph/Graph.h）：
//  - 图始终无环：connect() 拒绝成环的连线，求值只走拓扑序，不需要运行期环检测；
//  - 一个输入端口至多一根入线（P1 无列表语义，多入拼接留 P2）；
//  - 节点在 vector 里的顺序 = 画布 z 序；节点数据本身是纯数据 + 求值态，
//    序列化只存结构与标量，不存几何。
class Graph {
 public:
  struct Node {
    NodeId id = kInvalidNodeId;
    std::string type_id;                  // 注册表 id；加载对不上时 type 为空
    const NodeTypeInfo* type = nullptr;   // nullptr = 丢失节点（保数据、报错态）
    double x = 0.0;                       // 画布位置（世界坐标，随缩放平移）
    double y = 0.0;
    std::vector<NodeValue> input_values;  // 未连线端口的内联值（按输入端口序）
    std::vector<NodeValue> params;        // 参数值（按 NodeParamSpec 序；下拉/滑条这类）

    // —— 求值态（不序列化）——
    std::vector<NodeValue> outputs;  // 按 solve 后的输出端口序
    NodeState state = NodeState::Ok;
    std::string message;  // Warning / Error 的说明文字
    bool dirty = true;
    double last_millis = 0.0;    // 上一次 solve 耗时（成本模型用）
    std::uint64_t stamp = 0;     // 每次置脏递增；P2 后台求值回填时校验
    bool preview = false;        // 预览眼睛开关
  };

  // —— 结构编辑（都走这里，undo 命令在外面包一层）——
  // 注册表查得到的类型挂上 type 指针；查不到照建（丢失节点），加载旧档用。
  NodeId add_node(std::string type_id, double x, double y);
  // 删除节点（连带其全部连线）。节点不存在返回 false。
  bool remove_node(NodeId id);
  // 保留 id / 位置 / 端口值原样重插（undo 用）。
  Node* insert_node(std::unique_ptr<Node> node);

  // 连线。六个检查：端点存在、端口序号合法、类型可转换（can_convert）、
  // 输入端口没有被占、没有重复线、不成环（to 沿下游能到 from 则拒绝）。
  // why 非空时写入人话原因（给 toast）。
  bool connect(NodeId from_node, std::uint16_t from_port, NodeId to_node,
               std::uint16_t to_port, std::string* why = nullptr);
  // 按端点摘线；没这根线返回 false。
  bool disconnect(NodeId from_node, std::uint16_t from_port, NodeId to_node,
                  std::uint16_t to_port);
  // 摘掉某个节点某个输入端口上的线（拖端口拔线用）。返回是否摘到。
  bool disconnect_input(NodeId to_node, std::uint16_t to_port);

  // —— 查询 ——
  [[nodiscard]] Node* node(NodeId id);
  [[nodiscard]] const Node* node(NodeId id) const;
  [[nodiscard]] const std::vector<std::unique_ptr<Node>>& nodes() const { return nodes_; }
  [[nodiscard]] const std::vector<Link>& links() const { return links_; }
  // to 节点 to_port 输入端口上的那根入线（P1 至多一根）；没有返回 nullptr。
  [[nodiscard]] const Link* link_into(NodeId to_node, std::uint16_t to_port) const;
  [[nodiscard]] std::vector<const Link*> links_from(NodeId from_node) const;
  // from 沿下游方向能否到达 target（迭代 DFS；connect 的成环检查用）。
  [[nodiscard]] bool reaches(NodeId from, NodeId target) const;
  [[nodiscard]] NodeId next_id() const { return next_id_; }
  void set_next_id(NodeId id) { next_id_ = id > 0 ? id : 1; }
  [[nodiscard]] std::uint64_t version() const { return version_; }       // 结构版本
  [[nodiscard]] std::uint64_t eval_version() const { return eval_version_; }  // 求值版本

  // —— 求值支撑（实现在 node_eval.cpp 一侧的意思：见 evaluate_graph）——
  // 把 to_node（含全部下游）标脏；每次标脏递增 stamp（P2 后台回填校验用）。
  void mark_dirty(NodeId id);
  void mark_all_dirty();
  // Kahn 拓扑序。就绪队列按节点 vector 序入队，保证结果稳定可复现。
  [[nodiscard]] std::vector<NodeId> topological_order() const;
  // 脏节点的预估耗时（上一次 solve 的 last_millis 之和）；
  // 有从未解过的脏节点时返回 -1（成本未知，按贵的处理）。
  [[nodiscard]] double estimated_millis() const;
  // 求值完成后由 evaluate_graph 调用。
  void bump_eval_version() { ++eval_version_; }

  // 把所有「丢失节点」重查一次注册表（注册新类型后调用，比如插件加载完）。
  void resolve_types();

  // 画布 z 序：拖动的节点提到最前。
  void raise_node(NodeId id);

 private:
  std::vector<std::unique_ptr<Node>> nodes_;
  std::vector<Link> links_;
  NodeId next_id_ = 1;
  std::uint64_t version_ = 0;
  std::uint64_t eval_version_ = 0;
  std::uint64_t stamp_seq_ = 0;
};

}  // namespace tamias
