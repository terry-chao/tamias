#include "command/nodes/graph_edit_commands.h"

#include <algorithm>
#include <utility>

namespace tamias {

namespace {

// 深拷贝一个节点（Graph::Node 是纯数据，字段全公开，直接拷）。
std::unique_ptr<Graph::Node> clone_node(const Graph::Node& node) {
  return std::make_unique<Graph::Node>(node);
}

// 节点当前挂着的全部连线（进出都算；删除前快照用）。
std::vector<Link> links_touching(const Graph& graph, std::uint64_t id) {
  std::vector<Link> out;
  for (const Link& link : graph.links()) {
    if (link.from_node == id || link.to_node == id) {
      out.push_back(link);
    }
  }
  return out;
}

// 按快照把节点们放回去并重连线（undo 用）。连线照走 connect 的检查，
// 拍下来时合法的线放回去也合法；万一失败（理论上不会）静默跳过。
void restore_nodes(Graph& graph, std::vector<std::unique_ptr<Graph::Node>> nodes,
                   const std::vector<Link>& links) {
  for (auto& node : nodes) {
    graph.insert_node(std::move(node));
  }
  for (const Link& link : links) {
    (void)graph.connect(link.from_node, link.from_port, link.to_node, link.to_port);
  }
}

}  // namespace

// ===== NodeAddCommand =====

NodeAddCommand::NodeAddCommand(Document& document, std::string type_id, double x, double y)
    : document_(&document), type_id_(std::move(type_id)), x_(x), y_(y) {}

Result<void> NodeAddCommand::execute() {
  if (document_ == nullptr) {
    return Err("NodeAddCommand: no document");
  }
  node_id_ = document_->node_graph().add_node(type_id_, x_, y_);
  return {};
}

void NodeAddCommand::undo() {
  Graph* graph = document_->find_node_graph();
  if (graph == nullptr || graph->node(node_id_) == nullptr) {
    return;
  }
  snapshot_ = clone_node(*graph->node(node_id_));
  snapshot_links_ = links_touching(*graph, node_id_);
  graph->remove_node(node_id_);
}

void NodeAddCommand::redo() {
  if (snapshot_ == nullptr) {
    return;
  }
  std::vector<std::unique_ptr<Graph::Node>> nodes;
  nodes.push_back(std::move(snapshot_));
  restore_nodes(document_->node_graph(), std::move(nodes), snapshot_links_);
  snapshot_links_.clear();
}

// ===== NodeDeleteCommand =====

NodeDeleteCommand::NodeDeleteCommand(Document& document, std::vector<std::uint64_t> node_ids)
    : document_(&document), node_ids_(std::move(node_ids)) {}

void NodeDeleteCommand::snapshot_and_remove(Graph& graph) {
  snapshot_.clear();
  snapshot_links_.clear();
  for (const std::uint64_t id : node_ids_) {
    const Graph::Node* node = graph.node(id);
    if (node == nullptr) {
      continue;
    }
    snapshot_.push_back(clone_node(*node));
    for (const Link& link : links_touching(graph, id)) {
      // 两个端点都在删除集里的线只存一份。
      const bool both =
          std::any_of(node_ids_.begin(), node_ids_.end(), [&](std::uint64_t other) {
            return other != id && (link.from_node == other || link.to_node == other);
          });
      if (!both) {
        snapshot_links_.push_back(link);
      }
    }
  }
  for (const std::uint64_t id : node_ids_) {
    graph.remove_node(id);
  }
}

Result<void> NodeDeleteCommand::execute() {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return Err("NodeDeleteCommand: document has no node graph");
  }
  const bool any =
      std::any_of(node_ids_.begin(), node_ids_.end(),
                  [graph](std::uint64_t id) { return graph->node(id) != nullptr; });
  if (!any) {
    return Err("NodeDeleteCommand: nothing to delete");
  }
  snapshot_and_remove(*graph);
  return {};
}

void NodeDeleteCommand::undo() {
  if (snapshot_.empty() || document_->find_node_graph() == nullptr) {
    return;
  }
  restore_nodes(document_->node_graph(), std::move(snapshot_), snapshot_links_);
  snapshot_links_.clear();
}

void NodeDeleteCommand::redo() {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return;
  }
  snapshot_and_remove(*graph);
}

// ===== NodeConnectCommand =====

NodeConnectCommand::NodeConnectCommand(Document& document, std::uint64_t from_node,
                                       std::uint16_t from_port, std::uint64_t to_node,
                                       std::uint16_t to_port)
    : document_(&document),
      from_node_(from_node),
      from_port_(from_port),
      to_node_(to_node),
      to_port_(to_port) {}

Result<void> NodeConnectCommand::execute() {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return Err("NodeConnectCommand: document has no node graph");
  }
  std::string why;
  if (!graph->connect(from_node_, from_port_, to_node_, to_port_, &why)) {
    return Err("NodeConnectCommand: " + why);
  }
  return {};
}

void NodeConnectCommand::undo() {
  if (Graph* graph = document_->find_node_graph()) {
    graph->disconnect(from_node_, from_port_, to_node_, to_port_);
  }
}

void NodeConnectCommand::redo() {
  if (Graph* graph = document_->find_node_graph()) {
    (void)graph->connect(from_node_, from_port_, to_node_, to_port_);
  }
}

// ===== NodeDisconnectInputCommand =====

NodeDisconnectInputCommand::NodeDisconnectInputCommand(Document& document,
                                                       std::uint64_t to_node,
                                                       std::uint16_t to_port)
    : document_(&document), to_node_(to_node), to_port_(to_port) {}

Result<void> NodeDisconnectInputCommand::execute() {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return Err("NodeDisconnectInputCommand: document has no node graph");
  }
  const Link* link = graph->link_into(to_node_, to_port_);
  if (link == nullptr) {
    return Err("NodeDisconnectInputCommand: port has no link");
  }
  removed_ = *link;
  graph->disconnect(removed_.from_node, removed_.from_port, to_node_, to_port_);
  return {};
}

void NodeDisconnectInputCommand::undo() {
  if (Graph* graph = document_->find_node_graph()) {
    (void)graph->connect(removed_.from_node, removed_.from_port, removed_.to_node,
                         removed_.to_port);
  }
}

void NodeDisconnectInputCommand::redo() {
  if (Graph* graph = document_->find_node_graph()) {
    graph->disconnect(removed_.from_node, removed_.from_port, removed_.to_node,
                      removed_.to_port);
  }
}

// ===== NodeSetInputCommand =====

NodeSetInputCommand::NodeSetInputCommand(Document& document, std::uint64_t node_id,
                                         std::uint16_t port, NodeValue value,
                                         NodeValue previous_value)
    : document_(&document),
      node_id_(node_id),
      port_(port),
      new_value_(std::move(value)),
      old_value_(std::move(previous_value)),
      old_valid_(!old_value_.is_empty()) {}

Result<void> NodeSetInputCommand::apply() {
  Graph::Node* node = document_->find_node_graph() == nullptr
                          ? nullptr
                          : document_->find_node_graph()->node(node_id_);
  if (node == nullptr) {
    return Err("NodeSetInputCommand: node not found");
  }
  if (port_ >= node->input_values.size()) {
    node->input_values.resize(port_ + 1);
  }
  if (!old_valid_) {
    old_value_ = node->input_values[port_];
    old_valid_ = true;
  }
  node->input_values[port_] = new_value_;
  // 有连线时内联值不参与求值，但标脏无害且省一次判断。
  document_->find_node_graph()->mark_dirty(node_id_);
  return {};
}

Result<void> NodeSetInputCommand::execute() {
  return apply();
}

void NodeSetInputCommand::undo() {
  if (!old_valid_) {
    return;
  }
  if (Graph::Node* node =
          document_->find_node_graph() ? document_->find_node_graph()->node(node_id_) : nullptr) {
    node->input_values[port_] = old_value_;
    document_->find_node_graph()->mark_dirty(node_id_);
  }
}

void NodeSetInputCommand::redo() {
  (void)apply();
}

// ===== NodeSetParamCommand =====

NodeSetParamCommand::NodeSetParamCommand(Document& document, std::uint64_t node_id,
                                         std::uint16_t param, NodeValue value,
                                         NodeValue previous_value)
    : document_(&document),
      node_id_(node_id),
      param_(param),
      new_value_(std::move(value)),
      old_value_(std::move(previous_value)),
      old_valid_(!old_value_.is_empty()) {}

Result<void> NodeSetParamCommand::apply() {
  Graph::Node* node = document_->find_node_graph() == nullptr
                          ? nullptr
                          : document_->find_node_graph()->node(node_id_);
  if (node == nullptr) {
    return Err("NodeSetParamCommand: node not found");
  }
  if (param_ >= node->params.size()) {
    node->params.resize(param_ + 1);
  }
  if (!old_valid_) {
    old_value_ = node->params[param_];
    old_valid_ = true;
  }
  node->params[param_] = new_value_;
  document_->find_node_graph()->mark_dirty(node_id_);
  return {};
}

Result<void> NodeSetParamCommand::execute() {
  return apply();
}

void NodeSetParamCommand::undo() {
  if (!old_valid_) {
    return;
  }
  if (Graph::Node* node =
          document_->find_node_graph() ? document_->find_node_graph()->node(node_id_) : nullptr) {
    node->params[param_] = old_value_;
    document_->find_node_graph()->mark_dirty(node_id_);
  }
}

void NodeSetParamCommand::redo() {
  (void)apply();
}

// ===== NodeMoveCommand =====

NodeMoveCommand::NodeMoveCommand(Document& document, std::vector<Move> moves)
    : document_(&document), moves_(std::move(moves)) {}

void NodeMoveCommand::apply(bool to_final) {
  Graph* graph = document_ == nullptr ? nullptr : document_->find_node_graph();
  if (graph == nullptr) {
    return;
  }
  for (const Move& move : moves_) {
    if (Graph::Node* node = graph->node(move.node_id)) {
      node->x = to_final ? move.to_x : move.from_x;
      node->y = to_final ? move.to_y : move.from_y;
    }
  }
}

Result<void> NodeMoveCommand::execute() {
  if (moves_.empty()) {
    return Err("NodeMoveCommand: no moves");
  }
  apply(true);
  return {};
}

void NodeMoveCommand::undo() {
  apply(false);
}

void NodeMoveCommand::redo() {
  apply(true);
}

}  // namespace tamias
