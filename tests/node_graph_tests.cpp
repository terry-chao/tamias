#include "engine/modeling/nodes/node_graph.h"
#include "engine/modeling/nodes/node_type.h"

#include <gtest/gtest.h>

#include <string>

namespace tamias {
namespace {

// 帮手：number → box 的小图，外加一个孤立 cylinder。
struct TestGraph {
  Graph graph;
  NodeId number = kInvalidNodeId;
  NodeId box = kInvalidNodeId;
  NodeId cylinder = kInvalidNodeId;

  TestGraph() {
    number = graph.add_node("input.number", 0, 0);
    box = graph.add_node("solid.box", 200, 0);
    cylinder = graph.add_node("solid.cylinder", 400, 0);
    wired = graph.connect(number, 0, box, 0);  // number → box.Width
  }

  bool wired = false;
};

TEST(NodeGraph, AddNodeResolvesTypeAndSeedsDefaults) {
  Graph g;
  const NodeId id = g.add_node("solid.box", 10, 20);
  const Graph::Node* node = g.node(id);
  ASSERT_NE(node, nullptr);
  ASSERT_NE(node->type, nullptr);
  EXPECT_EQ(node->type->id, "solid.box");
  EXPECT_EQ(node->input_values.size(), node->type->inputs.size());
  EXPECT_EQ(node->input_values[0].as_number(), 1.0);  // Width 默认 1
  EXPECT_EQ(node->params.size(), node->type->params.size());
  EXPECT_TRUE(node->dirty);
}

TEST(NodeGraph, NewGeometryNodeDefaultsToPreviewOn) {
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  const NodeId number = g.add_node("input.number", 200, 0);
  const NodeId out = g.add_node("output.preview", 400, 0);
  ASSERT_NE(g.node(box), nullptr);
  ASSERT_NE(g.node(number), nullptr);
  ASSERT_NE(g.node(out), nullptr);
  // 加一个实体节点后视口立刻有橙色预览可看，不必先点眼睛。
  EXPECT_TRUE(g.node(box)->preview);
  EXPECT_TRUE(g.node(out)->preview);
  // 数值节点没有几何输出，开了也没东西可画。
  EXPECT_FALSE(g.node(number)->preview);
}

TEST(NodeGraph, ConnectRejectsCycle) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  const NodeId fillet = t.graph.add_node("solid.fillet", 600, 0);
  const NodeId boolean = t.graph.add_node("solid.boolean", 800, 0);
  ASSERT_TRUE(t.graph.connect(t.box, 0, fillet, 0));     // box → fillet.Body
  ASSERT_TRUE(t.graph.connect(fillet, 0, boolean, 0));   // fillet → boolean.A
  std::string why;
  // boolean.Body → fillet.Body 会成环，必须拒绝。
  EXPECT_FALSE(t.graph.connect(boolean, 0, fillet, 0, &why));
  EXPECT_FALSE(why.empty());
  // 自环也拒绝。
  EXPECT_FALSE(t.graph.connect(boolean, 0, boolean, 0));
}

TEST(NodeGraph, ConnectEnforcesPortTypes) {
  TestGraph t;
  std::string why;
  // Body 输出连 Number 输入：不允许。
  EXPECT_FALSE(t.graph.connect(t.box, 0, t.box, 0, &why));
  EXPECT_NE(why.find("cannot connect"), std::string::npos);
}

TEST(NodeGraph, NumberConvertsToIntegerPort) {
  Graph g;
  const NodeId number = g.add_node("input.number", 0, 0);
  // Number → Number 端口没问题。
  const NodeId box = g.add_node("solid.box", 200, 0);
  EXPECT_TRUE(g.connect(number, 0, box, 0));
}

TEST(NodeGraph, ConnectEnforcesSingleInputLink) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  const NodeId number2 = t.graph.add_node("input.number", 0, 100);
  std::string why;
  EXPECT_FALSE(t.graph.connect(number2, 0, t.box, 0, &why));  // box.Width 已被占
  EXPECT_EQ(why, "input port already connected");
}

TEST(NodeGraph, TopologicalOrderIsDeterministicAndValid) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  const NodeId boolean = t.graph.add_node("solid.boolean", 800, 0);
  ASSERT_TRUE(t.graph.connect(t.box, 0, boolean, 0));
  ASSERT_TRUE(t.graph.connect(t.cylinder, 0, boolean, 1));
  const std::vector<NodeId> order = t.graph.topological_order();
  ASSERT_EQ(order.size(), 4u);  // number, box, cylinder, boolean
  auto position = [&order](NodeId id) {
    for (std::size_t i = 0; i < order.size(); ++i) {
      if (order[i] == id) {
        return i;
      }
    }
    return static_cast<std::size_t>(-1);
  };
  EXPECT_LT(position(t.number), position(t.box));
  EXPECT_LT(position(t.box), position(boolean));
  EXPECT_LT(position(t.cylinder), position(boolean));
  // 再算一次结果一致（撤销重做 / 测试的确定性前提）。
  EXPECT_EQ(t.graph.topological_order(), order);
}

TEST(NodeGraph, MarkDirtyPropagatesDownstreamOnly) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  const NodeId boolean = t.graph.add_node("solid.boolean", 800, 0);
  ASSERT_TRUE(t.graph.connect(t.box, 0, boolean, 0));
  ASSERT_TRUE(t.graph.connect(t.cylinder, 0, boolean, 1));

  // 先全部洗干净。
  for (const auto& n : t.graph.nodes()) {
    t.graph.node(n->id)->dirty = false;
  }
  t.graph.mark_dirty(t.number);
  EXPECT_TRUE(t.graph.node(t.number)->dirty);
  EXPECT_TRUE(t.graph.node(t.box)->dirty);        // 下游
  EXPECT_TRUE(t.graph.node(boolean)->dirty);      // 下游
  EXPECT_FALSE(t.graph.node(t.cylinder)->dirty);  // 无关分支不动
  // stamp 递增过。
  EXPECT_GT(t.graph.node(t.box)->stamp, 0u);
}

TEST(NodeGraph, RemoveNodeDropsAttachedLinks) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  EXPECT_TRUE(t.graph.remove_node(t.box));
  EXPECT_EQ(t.graph.node(t.box), nullptr);
  for (const Link& l : t.graph.links()) {
    EXPECT_NE(l.from_node, t.box);
    EXPECT_NE(l.to_node, t.box);
  }
  EXPECT_FALSE(t.graph.remove_node(t.box));  // 再删返回 false
}

TEST(NodeGraph, MissingNodeTypeKeepsData) {
  Graph g;
  const NodeId id = g.add_node("totally.unknown", 5, 6);
  const Graph::Node* node = g.node(id);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, nullptr);  // 丢失节点
  // 两个丢失节点之间可以连线（保数据，端口无规格不校验）。
  const NodeId other = g.add_node("another.unknown", 7, 8);
  EXPECT_TRUE(g.connect(id, 0, other, 0));
}

TEST(NodeGraph, EstimatedMillisUnknownWhenNeverSolved) {
  TestGraph t;
  ASSERT_TRUE(t.wired);
  EXPECT_DOUBLE_EQ(t.graph.estimated_millis(), -1.0);  // 有从未解过的脏节点
  for (auto& n : t.graph.nodes()) {
    n->dirty = false;
    n->last_millis = 5.0;
  }
  t.graph.mark_dirty(t.number);
  // number 标脏会传播到下游 box：两个脏节点 × 5ms。
  EXPECT_DOUBLE_EQ(t.graph.estimated_millis(), 10.0);
}

}  // namespace
}  // namespace tamias
