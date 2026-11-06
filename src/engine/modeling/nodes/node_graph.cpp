#include "engine/modeling/nodes/node_graph.h"

#include "engine/modeling/nodes/node_type.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace tamias {

namespace {

// 端口序号合法性 + 端口类型信息。丢失节点（type == nullptr）一律放行端口序号，
// 只卡端点存在——保数据要紧，别让旧档读进来就崩。
bool port_ok(const Graph::Node& node, std::uint16_t port, bool input) {
  if (node.type == nullptr) {
    return true;
  }
  const std::size_t count = input ? node.type->inputs.size() : node.type->outputs.size();
  return static_cast<std::size_t>(port) < count;
}

}  // namespace

NodeId Graph::add_node(std::string type_id, double x, double y) {
  auto node = std::make_unique<Node>();
  node->id = next_id_++;
  node->type_id = std::move(type_id);
  node->type = NodeRegistry::builtin().find(node->type_id);
  node->x = x;
  node->y = y;
  // 内联值按端口规格铺默认；丢失节点留空，求值时按 Error 处理。
  if (node->type != nullptr) {
    node->input_values.reserve(node->type->inputs.size());
    for (const PortSpec& spec : node->type->inputs) {
      node->input_values.push_back(spec.default_value);
    }
    node->params.reserve(node->type->params.size());
    for (const NodeParamSpec& spec : node->type->params) {
      node->params.push_back(spec.default_value);
    }
    node->outputs.assign(node->type->outputs.size(), NodeValue::empty());
    // 默认开预览：加一个 Box 立刻能在视口看到橙色预览体，而不是先去找眼睛按钮。
    // 只有带几何输出的节点才开（input.number 这类开了也没东西可画）。
    node->preview =
        std::any_of(node->type->outputs.begin(), node->type->outputs.end(),
                    [](const PortSpec& out) { return out.type == PortType::Body; });
  }
  const NodeId id = node->id;
  nodes_.push_back(std::move(node));
  ++version_;
  return id;
}

bool Graph::remove_node(NodeId id) {
  for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
    if ((*it)->id == id) {
      nodes_.erase(it);
      std::erase_if(links_, [id](const Link& l) {
        return l.from_node == id || l.to_node == id;
      });
      ++version_;
      return true;
    }
  }
  return false;
}

Graph::Node* Graph::insert_node(std::unique_ptr<Node> inserted) {
  if (inserted == nullptr || inserted->id == kInvalidNodeId) {
    return nullptr;
  }
  // 丢失类型重查一次注册表（注册表可能在那之后又学会了这个类型）。
  if (inserted->type == nullptr) {
    inserted->type = NodeRegistry::builtin().find(inserted->type_id);
  }
  next_id_ = std::max(next_id_, inserted->id + 1);
  const NodeId id = inserted->id;
  nodes_.push_back(std::move(inserted));
  ++version_;
  return node(id);
}

bool Graph::connect(NodeId from_node, std::uint16_t from_port, NodeId to_node,
                    std::uint16_t to_port, std::string* why) {
  const Node* from = node(from_node);
  const Node* to = node(to_node);
  if (from == nullptr || to == nullptr) {
    if (why != nullptr) {
      *why = "endpoint node missing";
    }
    return false;
  }
  if (!port_ok(*from, from_port, false) || !port_ok(*to, to_port, true)) {
    if (why != nullptr) {
      *why = "port index out of range";
    }
    return false;
  }
  // 类型相容（丢失节点无规格，放行）。
  if (from->type != nullptr && to->type != nullptr) {
    const PortType out_type = from->type->outputs[from_port].type;
    const PortType in_type = to->type->inputs[to_port].type;
    if (!can_convert(out_type, in_type)) {
      if (why != nullptr) {
        *why = std::string("cannot connect ") + to_string(out_type) + " to " +
               to_string(in_type);
      }
      return false;
    }
  }
  // P1：一个输入端口至多一根入线。
  if (link_into(to_node, to_port) != nullptr) {
    if (why != nullptr) {
      *why = "input port already connected";
    }
    return false;
  }
  for (const Link& l : links_) {
    if (l.from_node == from_node && l.from_port == from_port && l.to_node == to_node &&
        l.to_port == to_port) {
      if (why != nullptr) {
        *why = "duplicate link";
      }
      return false;
    }
  }
  // 成环检查：to 沿下游能回到 from 就拒绝。
  if (reaches(to_node, from_node) || to_node == from_node) {
    if (why != nullptr) {
      *why = "connection would create a cycle";
    }
    return false;
  }
  links_.push_back(Link{from_node, from_port, to_node, to_port});
  mark_dirty(to_node);
  ++version_;
  return true;
}

bool Graph::disconnect(NodeId from_node, std::uint16_t from_port, NodeId to_node,
                       std::uint16_t to_port) {
  for (auto it = links_.begin(); it != links_.end(); ++it) {
    if (it->from_node == from_node && it->from_port == from_port && it->to_node == to_node &&
        it->to_port == to_port) {
      links_.erase(it);
      mark_dirty(to_node);
      ++version_;
      return true;
    }
  }
  return false;
}

bool Graph::disconnect_input(NodeId to_node, std::uint16_t to_port) {
  const Link* l = link_into(to_node, to_port);
  if (l == nullptr) {
    return false;
  }
  return disconnect(l->from_node, l->from_port, to_node, to_port);
}

Graph::Node* Graph::node(NodeId id) {
  for (auto& n : nodes_) {
    if (n->id == id) {
      return n.get();
    }
  }
  return nullptr;
}

const Graph::Node* Graph::node(NodeId id) const {
  for (auto& n : nodes_) {
    if (n->id == id) {
      return n.get();
    }
  }
  return nullptr;
}

const Link* Graph::link_into(NodeId to_node, std::uint16_t to_port) const {
  for (const Link& l : links_) {
    if (l.to_node == to_node && l.to_port == to_port) {
      return &l;
    }
  }
  return nullptr;
}

std::vector<const Link*> Graph::links_from(NodeId from_node) const {
  std::vector<const Link*> out;
  for (const Link& l : links_) {
    if (l.from_node == from_node) {
      out.push_back(&l);
    }
  }
  return out;
}

bool Graph::reaches(NodeId from, NodeId target) const {
  if (from == target) {
    return true;
  }
  std::vector<NodeId> stack{from};
  std::unordered_set<NodeId> seen{from};
  while (!stack.empty()) {
    const NodeId cur = stack.back();
    stack.pop_back();
    for (const Link& l : links_) {
      if (l.from_node != cur || seen.count(l.to_node) != 0) {
        continue;
      }
      if (l.to_node == target) {
        return true;
      }
      seen.insert(l.to_node);
      stack.push_back(l.to_node);
    }
  }
  return false;
}

void Graph::mark_dirty(NodeId id) {
  // 迭代 DFS：把 id 及其全部下游标脏。每标一次递增 stamp，
  // P2 后台求值结束时只回填 stamp 没变过的节点。
  std::vector<NodeId> stack{id};
  std::unordered_set<NodeId> seen{id};
  while (!stack.empty()) {
    const NodeId cur = stack.back();
    stack.pop_back();
    if (Node* n = node(cur)) {
      n->dirty = true;
      n->stamp = ++stamp_seq_;
    }
    for (const Link& l : links_) {
      if (l.from_node == cur && seen.insert(l.to_node).second) {
        stack.push_back(l.to_node);
      }
    }
  }
}

void Graph::mark_all_dirty() {
  for (auto& n : nodes_) {
    n->dirty = true;
    n->stamp = ++stamp_seq_;
  }
}

std::vector<NodeId> Graph::topological_order() const {
  // Kahn：入度表 + 就绪队列。队列按节点 vector 序推进（先入先出），
  // 同层结果稳定可复现——撤销重做 / 测试都靠这个确定性。
  std::unordered_map<NodeId, std::size_t> index;
  index.reserve(nodes_.size());
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    index[nodes_[i]->id] = i;
  }
  std::vector<std::size_t> in_degree(nodes_.size(), 0);
  for (const Link& l : links_) {
    const auto it = index.find(l.to_node);
    if (it != index.end()) {
      ++in_degree[it->second];
    }
  }
  std::vector<std::size_t> ready;
  for (std::size_t i = 0; i < nodes_.size(); ++i) {
    if (in_degree[i] == 0) {
      ready.push_back(i);
    }
  }
  std::vector<NodeId> order;
  order.reserve(nodes_.size());
  for (std::size_t head = 0; head < ready.size(); ++head) {
    const std::size_t at = ready[head];
    order.push_back(nodes_[at]->id);
    for (const Link& l : links_) {
      if (l.from_node != nodes_[at]->id) {
        continue;
      }
      const auto it = index.find(l.to_node);
      if (it == index.end()) {
        continue;
      }
      if (--in_degree[it->second] == 0) {
        ready.push_back(it->second);
      }
    }
  }
  return order;
}

double Graph::estimated_millis() const {
  double total = 0.0;
  for (const auto& n : nodes_) {
    if (!n->dirty) {
      continue;
    }
    if (n->last_millis <= 0.0) {
      return -1.0;  // 从未解过的脏节点：成本未知，按贵的处理。
    }
    total += n->last_millis;
  }
  return total;
}

void Graph::resolve_types() {
  for (auto& n : nodes_) {
    if (n->type == nullptr) {
      n->type = NodeRegistry::builtin().find(n->type_id);
    }
  }
}

void Graph::raise_node(NodeId id) {
  for (auto it = nodes_.begin(); it != nodes_.end(); ++it) {
    if ((*it)->id == id) {
      std::unique_ptr<Node> moved = std::move(*it);
      nodes_.erase(it);
      nodes_.push_back(std::move(moved));
      ++version_;
      return;
    }
  }
}

}  // namespace tamias
