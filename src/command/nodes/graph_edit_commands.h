#pragma once

#include "command/core/command.h"
#include "engine/document/document.h"
#include "engine/modeling/nodes/node_graph.h"
#include "engine/modeling/nodes/node_value.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace tamias {

// ===== 节点图编辑命令（全部可撤销，走文档 CommandStack；求值不入撤销——重算即可）=====
// 约定与 Graph 一致：结构编辑都经过 Graph 的 connect / remove（成环、类型、
// 单入线检查在那里做），命令只负责「改之前拍快照，撤销时原样放回」。

// 添加一个节点。undo 拍下当时的状态（可能已被后续编辑命令改过、又被逐个撤销回来），
// redo 按 id 原样放回。
class NodeAddCommand final : public Command {
 public:
  NodeAddCommand(Document& document, std::string type_id, double x, double y);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

  [[nodiscard]] std::uint64_t node_id() const { return node_id_; }

 private:
  Document* document_ = nullptr;
  std::string type_id_;
  double x_ = 0.0;
  double y_ = 0.0;
  std::uint64_t node_id_ = kInvalidNodeId;
  std::unique_ptr<Graph::Node> snapshot_;  // undo 时拍，redo 用
  std::vector<Link> snapshot_links_;
};

// 删除一组节点（连带其全部连线）。
class NodeDeleteCommand final : public Command {
 public:
  NodeDeleteCommand(Document& document, std::vector<std::uint64_t> node_ids);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  // 拍快照（节点 + 触碰的连线）然后删除；execute 与 redo 共用。
  void snapshot_and_remove(Graph& graph);

  Document* document_ = nullptr;
  std::vector<std::uint64_t> node_ids_;
  std::vector<std::unique_ptr<Graph::Node>> snapshot_;
  std::vector<Link> snapshot_links_;
};

// 连一根线（Graph::connect 的检查照走，成环 / 类型不符会作为命令错误返回）。
class NodeConnectCommand final : public Command {
 public:
  NodeConnectCommand(Document& document, std::uint64_t from_node, std::uint16_t from_port,
                     std::uint64_t to_node, std::uint16_t to_port);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t from_node_ = 0;
  std::uint16_t from_port_ = 0;
  std::uint64_t to_node_ = 0;
  std::uint16_t to_port_ = 0;
};

// 拔掉一个输入端口上的线。
class NodeDisconnectInputCommand final : public Command {
 public:
  NodeDisconnectInputCommand(Document& document, std::uint64_t to_node,
                             std::uint16_t to_port);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Document* document_ = nullptr;
  std::uint64_t to_node_ = 0;
  std::uint16_t to_port_ = 0;
  Link removed_{};  // execute 时记下拔掉的线
};

// 改一个未连线端口的内联值（滑条 / 字段编辑）。会触发下游重算。
class NodeSetInputCommand final : public Command {
 public:
  // previous_value：拖拽（scrub）场景下 UI 已经把值实时改到位，这里只补撤销记录。
  // 不传时 execute() 自己拍当前值作为撤销基准。
  NodeSetInputCommand(Document& document, std::uint64_t node_id, std::uint16_t port,
                      NodeValue value, NodeValue previous_value = NodeValue::empty());

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Result<void> apply();

  Document* document_ = nullptr;
  std::uint64_t node_id_ = 0;
  std::uint16_t port_ = 0;
  NodeValue old_value_;
  NodeValue new_value_;
  bool old_valid_ = false;
};

// 改一个参数值（下拉 / 滑条）。
class NodeSetParamCommand final : public Command {
 public:
  // 同 NodeSetInputCommand 的 previous_value。
  NodeSetParamCommand(Document& document, std::uint64_t node_id, std::uint16_t param,
                      NodeValue value, NodeValue previous_value = NodeValue::empty());

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  Result<void> apply();

  Document* document_ = nullptr;
  std::uint64_t node_id_ = 0;
  std::uint16_t param_ = 0;
  NodeValue old_value_;
  NodeValue new_value_;
  bool old_valid_ = false;
};

// 移动一组节点（只改画布位置，不触发求值）。UI 拖拽期间直接改 x/y（预览），
// 松手时用「起点 → 终点」构造本命令补一条撤销记录。
class NodeMoveCommand final : public Command {
 public:
  struct Move {
    std::uint64_t node_id = 0;
    double from_x = 0.0;
    double from_y = 0.0;
    double to_x = 0.0;
    double to_y = 0.0;
  };

  NodeMoveCommand(Document& document, std::vector<Move> moves);

  [[nodiscard]] Result<void> execute() override;
  void undo() override;
  void redo() override;

 private:
  void apply(bool to_final);

  Document* document_ = nullptr;
  std::vector<Move> moves_;
};

}  // namespace tamias
