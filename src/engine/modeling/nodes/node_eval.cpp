#include "engine/modeling/nodes/node_eval.h"

#include "engine/modeling/nodes/node_type.h"

#include <chrono>
#include <utility>

namespace tamias {

// evaluate_graph 填 SolveArgs 的通道（SolveArgs 的字段是私有的）。
// 注意不能放进匿名命名空间——SolveArgs 里的 friend 声明认的是 tamias::
// 这个名字，匿名命名空间里同名结构体是另一个类型。
struct SolveArgsAccessor {
  static void begin(SolveArgs& args, ModelKernel* kernel) {
    args.kernel_ = kernel;
    args.inputs_.clear();
    args.params_.clear();
    args.outputs_.clear();
    args.state_ = NodeState::Ok;
    args.message_.clear();
  }
  static void add_input(SolveArgs& args, std::optional<NodeValue> value) {
    args.inputs_.push_back(std::move(value));
  }
  static void add_param(SolveArgs& args, const std::string& key, NodeValue value) {
    args.params_.emplace_back(key, std::move(value));
  }
  static std::vector<NodeValue> take_outputs(SolveArgs& args) { return std::move(args.outputs_); }
  static NodeState state(const SolveArgs& args) { return args.state_; }
  static const std::string& message(const SolveArgs& args) { return args.message_; }
};

namespace {

// 汇聚一个输入端口的当前值：有连线取上游输出（转端口类型），
// 否则取内联值；两者皆空 → nullopt（缺输入）。
std::optional<NodeValue> gather_input(const Graph& graph, const Graph::Node& node,
                                      std::uint16_t port) {
  const PortSpec* spec = nullptr;
  if (node.type != nullptr && port < node.type->inputs.size()) {
    spec = &node.type->inputs[port];
  }
  if (const Link* link = graph.link_into(node.id, port)) {
    const Graph::Node* upstream = graph.node(link->from_node);
    if (upstream == nullptr) {
      return std::nullopt;
    }
    if (link->from_port >= upstream->outputs.size()) {
      return std::nullopt;
    }
    NodeValue value = upstream->outputs[link->from_port];
    if (value.is_empty()) {
      return std::nullopt;  // 上游还没算出来 / 上游错了。
    }
    if (spec != nullptr) {
      value = value.convert_to(spec->type);
    }
    return value;
  }
  NodeValue inline_value;
  if (port < node.input_values.size()) {
    inline_value = node.input_values[port];
  } else if (spec != nullptr) {
    inline_value = spec->default_value;
  }
  if (inline_value.is_empty()) {
    return std::nullopt;
  }
  return inline_value;
}

}  // namespace

NodeValue NodeValue::convert_to(PortType to) const {
  if (type() == to || to == PortType::Empty || is_empty()) {
    return *this;
  }
  switch (to) {
    case PortType::Number:
      if (type() == PortType::Integer || type() == PortType::Bool) {
        return NodeValue::number(as_number());
      }
      break;
    case PortType::Integer:
      if (type() == PortType::Number) {
        return NodeValue::integer(as_integer());
      }
      break;
    case PortType::Bool:
      if (type() == PortType::Number || type() == PortType::Integer) {
        return NodeValue::boolean(as_bool());
      }
      break;
    default:
      break;
  }
  return NodeValue::empty();
}

double SolveArgs::param_number(const std::string& key, double fallback) const {
  for (const auto& [k, v] : params_) {
    if (k == key) {
      return v.as_number(fallback);
    }
  }
  return fallback;
}

std::int64_t SolveArgs::param_integer(const std::string& key, std::int64_t fallback) const {
  for (const auto& [k, v] : params_) {
    if (k == key) {
      return v.as_integer(fallback);
    }
  }
  return fallback;
}

bool SolveArgs::param_bool(const std::string& key, bool fallback) const {
  for (const auto& [k, v] : params_) {
    if (k == key) {
      return v.as_bool(fallback);
    }
  }
  return fallback;
}

const std::string& SolveArgs::param_text(const std::string& key) const {
  static const std::string kEmpty;
  for (const auto& [k, v] : params_) {
    if (k == key) {
      return v.as_text();
    }
  }
  return kEmpty;
}

bool evaluate_graph(Graph& graph, SolveContext& context) {
  const std::vector<NodeId> order = graph.topological_order();
  for (const NodeId id : order) {
    if (context.cancelled && context.cancelled()) {
      return false;
    }
    Graph::Node* node = graph.node(id);
    if (node == nullptr || !node->dirty) {
      continue;  // 干净节点复用 outputs。
    }

    SolveArgs args;
    SolveArgsAccessor::begin(args, context.kernel);

    // 丢失节点：保数据但没法算。
    if (node->type == nullptr) {
      node->state = NodeState::Error;
      node->message = "unknown node type: " + node->type_id;
      node->outputs.clear();
      node->dirty = false;
      continue;
    }

    // 汇聚输入 + 参数。
    bool missing_required = false;
    std::string missing_name;
    for (std::size_t p = 0; p < node->type->inputs.size(); ++p) {
      std::optional<NodeValue> value = gather_input(graph, *node, static_cast<std::uint16_t>(p));
      if (!value.has_value() && !node->type->inputs[p].optional) {
        missing_required = true;
        missing_name = node->type->inputs[p].name;
      }
      SolveArgsAccessor::add_input(args, std::move(value));
    }
    // 参数按 NodeParamSpec 序取节点参数值；条目不够（旧档少了新参数）回默认值。
    for (std::size_t p = 0; p < node->type->params.size(); ++p) {
      const NodeValue value = p < node->params.size() ? node->params[p]
                                                      : node->type->params[p].default_value;
      SolveArgsAccessor::add_param(args, node->type->params[p].key, value);
    }

    if (missing_required) {
      // 缺必需输入：转 Warning，输出清空，下游继续（会同样报缺输入）。
      node->state = NodeState::Warning;
      node->message = "missing input: " + missing_name;
      node->outputs.assign(node->type->outputs.size(), NodeValue::empty());
      node->dirty = false;
      continue;
    }
    if (node->type->solve == nullptr) {
      // 展示型节点（output.preview）：把第一个输入透传到第一个输出，
      // 预览 / 烘焙收集就统一从「输出端口」取体。
      node->outputs.assign(node->type->outputs.size(), NodeValue::empty());
      if (!node->type->outputs.empty() && args.has(0)) {
        node->outputs[0] = args.in(0);
      }
      node->state = NodeState::Ok;
      node->message.clear();
      node->dirty = false;
      continue;
    }

    const auto t0 = std::chrono::steady_clock::now();
    try {
      node->type->solve(args);
    } catch (const std::exception& e) {
      args.error(std::string("exception: ") + e.what());
    } catch (...) {
      args.error("unknown exception in node solve");
    }
    const auto t1 = std::chrono::steady_clock::now();

    node->outputs = SolveArgsAccessor::take_outputs(args);
    if (node->outputs.size() < node->type->outputs.size()) {
      node->outputs.resize(node->type->outputs.size());
    }
    node->state = SolveArgsAccessor::state(args);
    node->message = SolveArgsAccessor::message(args);
    node->last_millis = std::chrono::duration<double, std::milli>(t1 - t0).count();
    node->dirty = false;

    if (context.solved) {
      context.solved(*node);
    }
  }
  graph.bump_eval_version();
  return true;
}

}  // namespace tamias
