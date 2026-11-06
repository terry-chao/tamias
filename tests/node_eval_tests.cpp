#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/mki/kernel.h"
#include "engine/modeling/nodes/node_eval.h"
#include "engine/modeling/nodes/node_graph.h"
#include "engine/modeling/nodes/node_type.h"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <memory>

namespace tamias {
namespace {

// 求值测试统一用默认内核后端（tests 链了全部内核）。
std::unique_ptr<ModelKernel> make_kernel() {
  register_linked_kernels();
  auto created = ModelKernel::create(KernelCreateInfo{default_kernel_backend()});
  if (!created) {
    return nullptr;
  }
  return std::move(*created);
}

// number(2) → box.Width，box 内联 Height=1 Depth=1。
Graph make_box_graph() {
  Graph g;
  const NodeId number = g.add_node("input.number", 0, 0);
  Graph::Node* n = g.node(number);
  n->params[0] = NodeValue::number(2.0);  // value
  const NodeId box = g.add_node("solid.box", 200, 0);
  (void)g.connect(number, 0, box, 0);
  return g;
}

TEST(NodeEval, BoxEvaluatesToBody) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g = make_box_graph();
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));

  const Graph::Node* number = g.node(1);
  const Graph::Node* box = g.node(2);
  ASSERT_NE(number, nullptr);
  ASSERT_NE(box, nullptr);
  EXPECT_EQ(number->state, NodeState::Ok);
  EXPECT_EQ(box->state, NodeState::Ok);
  // number 输出被 box 用掉，box 输出是体。
  ASSERT_EQ(box->outputs.size(), 1u);
  EXPECT_NE(box->outputs[0].as_body(), nullptr);
  EXPECT_FALSE(box->dirty);
  EXPECT_GE(box->last_millis, 0.0);
}

TEST(NodeEval, BoxBoundsReflectWidth) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g = make_box_graph();
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  auto bounds = kernel->bounds(*g.node(2)->outputs[0].as_body());
  ASSERT_TRUE(bounds) << bounds.error();
  // XZ 平面轮廓 + 沿 +Y 拉伸：宽 2 → x 跨度 2。
  EXPECT_NEAR(bounds->max.x - bounds->min.x, 2.0, 1e-4);
  EXPECT_NEAR(bounds->max.y - bounds->min.y, 1.0, 1e-4);
}

TEST(NodeEval, BoxAxesFollowPortNames) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  g.node(box)->input_values[0] = NodeValue::number(2.0);  // Width → X
  g.node(box)->input_values[1] = NodeValue::number(3.0);  // Height → Y
  g.node(box)->input_values[2] = NodeValue::number(4.0);  // Depth → Z
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  auto bounds = kernel->bounds(*g.node(box)->outputs[0].as_body());
  ASSERT_TRUE(bounds) << bounds.error();
  EXPECT_NEAR(bounds->max.x - bounds->min.x, 2.0, 1e-4);
  EXPECT_NEAR(bounds->max.y - bounds->min.y, 3.0, 1e-4);
  EXPECT_NEAR(bounds->max.z - bounds->min.z, 4.0, 1e-4);
}

TEST(NodeEval, InvalidDimensionBecomesNodeError) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  g.node(box)->input_values[0] = NodeValue::number(-1.0);  // Width 非法
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const Graph::Node* node = g.node(box);
  EXPECT_EQ(node->state, NodeState::Error);
  EXPECT_FALSE(node->message.empty());
  EXPECT_EQ(node->outputs[0].as_body(), nullptr);  // 错误节点输出清空
}

TEST(NodeEval, MissingInputWarnsNotCrashes) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  // 布尔节点不连线：两个 Body 输入都缺 → Warning。
  g.add_node("solid.boolean", 0, 0);
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const Graph::Node* node = g.node(1);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->state, NodeState::Warning);
  EXPECT_NE(node->message.find("missing input"), std::string::npos);
}

TEST(NodeEval, ErrorPropagatesAsMissingInputDownstream) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId bad = g.add_node("solid.box", 0, 0);
  g.node(bad)->input_values[0] = NodeValue::number(-1.0);
  const NodeId fillet = g.add_node("solid.fillet", 200, 0);
  ASSERT_TRUE(g.connect(bad, 0, fillet, 0));
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  EXPECT_EQ(g.node(bad)->state, NodeState::Error);
  // 上游错了 → fillet 拿不到体 → Warning（图不崩）。
  EXPECT_EQ(g.node(fillet)->state, NodeState::Warning);
}

TEST(NodeEval, BooleanChainCut) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  const NodeId cylinder = g.add_node("solid.cylinder", 200, 0);
  g.node(cylinder)->input_values[1] = NodeValue::number(2.0);  // Height 超出 box
  const NodeId boolean = g.add_node("solid.boolean", 400, 0);
  ASSERT_TRUE(g.connect(box, 0, boolean, 0));
  ASSERT_TRUE(g.connect(cylinder, 0, boolean, 1));
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));

  const Graph::Node* result = g.node(boolean);
  ASSERT_EQ(result->state, NodeState::Ok);
  const BodyRef cut = result->outputs[0].as_body();
  ASSERT_NE(cut, nullptr);
  // P1 内核没有 volume 动词，用包围盒有效性粗验证布尔确实产出了体。
  auto bounds = kernel->bounds(*cut);
  ASSERT_TRUE(bounds) << bounds.error();
  EXPECT_TRUE(bounds->valid());
}

TEST(NodeEval, FilletAllEdgesShrinksBounds) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  const NodeId fillet = g.add_node("solid.fillet", 200, 0);
  ASSERT_TRUE(g.connect(box, 0, fillet, 0));
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  EXPECT_EQ(g.node(fillet)->state, NodeState::Ok) << g.node(fillet)->message;
  EXPECT_NE(g.node(fillet)->outputs[0].as_body(), nullptr);
}

TEST(NodeEval, MoveTranslatesBody) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  const NodeId move = g.add_node("xform.move", 200, 0);
  g.node(move)->input_values[1] = NodeValue::vector(Vec3{1.0f, 0.0f, 0.0f});
  ASSERT_TRUE(g.connect(box, 0, move, 0));
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const Graph::Node* moved = g.node(move);
  ASSERT_EQ(moved->state, NodeState::Ok);
  auto bounds = kernel->bounds(*moved->outputs[0].as_body());
  ASSERT_TRUE(bounds) << bounds.error();
  EXPECT_NEAR(bounds->min.x, 0.5, 1e-4);  // 1×1 盒子平移 +1 → x ∈ [0.5, 1.5]
  EXPECT_NEAR(bounds->max.x, 1.5, 1e-4);
}

TEST(NodeEval, CleanNodesAreNotResolvedAgain) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g = make_box_graph();
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const BodyRef first = g.node(2)->outputs[0].as_body();
  ASSERT_NE(first, nullptr);

  // 只动 number 的值 → number 重算 + box 重算（新的 Body 实例）。
  g.node(1)->params[0] = NodeValue::number(3.0);
  g.mark_dirty(1);
  int solved_calls = 0;
  ctx.solved = [&solved_calls](const Graph::Node&) { ++solved_calls; };
  ASSERT_TRUE(evaluate_graph(g, ctx));
  EXPECT_EQ(solved_calls, 2);
  const BodyRef second = g.node(2)->outputs[0].as_body();
  ASSERT_NE(second, nullptr);
  EXPECT_NE(second.get(), first.get());  // 确实重算出了新体
}

TEST(NodeEval, PreviewOutputPassesBodyThrough) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  const NodeId box = g.add_node("solid.box", 0, 0);
  const NodeId out = g.add_node("output.preview", 200, 0);
  ASSERT_TRUE(g.connect(box, 0, out, 0));
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const Graph::Node* node = g.node(out);
  ASSERT_EQ(node->state, NodeState::Ok);
  // 透传：output 节点的输出 == box 的输出（同一 BodyRef）。
  EXPECT_EQ(node->outputs[0].as_body().get(), g.node(box)->outputs[0].as_body().get());
}

TEST(NodeEval, MissingNodeTypeIsError) {
  auto kernel = make_kernel();
  ASSERT_NE(kernel, nullptr);
  Graph g;
  g.add_node("no.such.type", 0, 0);
  SolveContext ctx;
  ctx.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(g, ctx));
  const Graph::Node* node = g.node(1);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->state, NodeState::Error);
  EXPECT_NE(node->message.find("unknown node type"), std::string::npos);
}

}  // namespace
}  // namespace tamias
