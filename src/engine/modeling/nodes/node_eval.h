#pragma once

#include "engine/base/result.h"
#include "engine/modeling/mki/kernel.h"
#include "engine/modeling/nodes/node_graph.h"

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace tamias {

// 求值上下文：图求值只要一个内核 + 两个可选回调。
// P1 全同步（UI 线程跑）；P2 后台求值时克隆图另开上下文，结构不变。
struct SolveContext {
  ModelKernel* kernel = nullptr;
  // 节点之间轮询；返回 true 立刻中止（长链可以停）。
  std::function<bool()> cancelled;
  // 每解完一个节点回调一次（状态栏进度 / 增量刷新预览用）。
  std::function<void(const Graph::Node&)> solved;
};

// solve lambda 的适配器：节点体只跟它打交道，读当前输入、写输出、报状态。
// P1 是纯标量语义：每个输入端口一个值（连线取上游输出，否则内联值）。
struct SolveArgs {
 public:
  // —— 输入（resolve_inputs 填好；缺输入的端口是 nullopt）——
  [[nodiscard]] bool has(int port) const {
    return port >= 0 && static_cast<std::size_t>(port) < inputs_.size() &&
           inputs_[port].has_value();
  }
  [[nodiscard]] NodeValue in(int port) const {
    return has(port) ? *inputs_[port] : NodeValue::empty();
  }
  [[nodiscard]] double in_number(int port, double fallback = 0.0) const {
    return has(port) ? in(port).as_number(fallback) : fallback;
  }
  [[nodiscard]] std::int64_t in_integer(int port, std::int64_t fallback = 0) const {
    return has(port) ? in(port).as_integer(fallback) : fallback;
  }
  [[nodiscard]] bool in_bool(int port, bool fallback = false) const {
    return has(port) ? in(port).as_bool(fallback) : fallback;
  }
  [[nodiscard]] const std::string& in_text(int port) const {
    static const std::string kEmpty;
    return has(port) ? in(port).as_text() : kEmpty;
  }
  [[nodiscard]] Vec3 in_vector(int port, Vec3 fallback = {}) const {
    return has(port) ? in(port).as_vector(fallback) : fallback;
  }
  [[nodiscard]] BodyRef in_body(int port) const { return has(port) ? in(port).as_body() : nullptr; }

  // 节点自己的参数（NodeParamSpec 的 key → 当前值；由 evaluate_graph 从节点参数表填）。
  [[nodiscard]] double param_number(const std::string& key, double fallback = 0.0) const;
  [[nodiscard]] std::int64_t param_integer(const std::string& key, std::int64_t fallback = 0) const;
  [[nodiscard]] bool param_bool(const std::string& key, bool fallback = false) const;
  [[nodiscard]] const std::string& param_text(const std::string& key) const;

  // —— 输出 / 状态 ——
  void out(int port, NodeValue value) {
    if (port >= 0) {
      const std::size_t at = static_cast<std::size_t>(port);
      if (at >= outputs_.size()) {
        outputs_.resize(at + 1);
      }
      outputs_[at] = std::move(value);
    }
  }
  void warn(std::string message) {
    if (state_ == NodeState::Ok) {
      state_ = NodeState::Warning;
    }
    message_ = std::move(message);
  }
  void error(std::string message) {
    state_ = NodeState::Error;
    message_ = std::move(message);
    outputs_.clear();
  }

  [[nodiscard]] ModelKernel* kernel() const { return kernel_; }

 private:
  friend struct SolveArgsAccessor;  // evaluate_graph 的填充口（不进公共 API）
  ModelKernel* kernel_ = nullptr;
  std::vector<std::optional<NodeValue>> inputs_;
  std::vector<std::pair<std::string, NodeValue>> params_;
  std::vector<NodeValue> outputs_;
  NodeState state_ = NodeState::Ok;
  std::string message_;
};

// 求值一张图：按拓扑序只解脏节点，跳过干净节点（复用其 outputs）。
// 节点 Error → 输出清空；下游缺必需输入 → Warning「缺少输入」并把它的下游标脏。
// 返回是否完整跑完（cancelled 中止返回 false）。
[[nodiscard]] bool evaluate_graph(Graph& graph, SolveContext& context);

}  // namespace tamias
