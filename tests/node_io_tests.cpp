#include "engine/modeling/nodes/node_graph.h"
#include "engine/modeling/nodes/node_io.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

Graph make_sample_graph() {
  Graph g;
  const NodeId number = g.add_node("input.number", 0, 0);
  g.node(number)->params[0] = NodeValue::number(2.5);   // value
  g.node(number)->params[2] = NodeValue::number(7.0);   // max
  const NodeId box = g.add_node("solid.box", 200, 0);
  g.node(box)->input_values[1] = NodeValue::number(1.5);  // Height 内联值
  (void)g.connect(number, 0, box, 0);
  const NodeId boolean = g.add_node("solid.boolean", 400, 0);
  (void)g.connect(box, 0, boolean, 0);
  g.node(boolean)->params[0] = NodeValue::text("cut");   // op
  g.node(box)->preview = true;
  return g;
}

TEST(NodeIo, RoundTripPreservesStructureAndValues) {
  Graph g = make_sample_graph();
  auto bytes = write_node_graph(g);
  ASSERT_TRUE(bytes) << bytes.error();

  Graph loaded;
  auto ok = read_node_graph(loaded, *bytes);
  ASSERT_TRUE(ok) << ok.error();

  ASSERT_EQ(loaded.nodes().size(), g.nodes().size());
  ASSERT_EQ(loaded.links().size(), 2u);  // number→box.Width、box→boolean.A

  const Graph::Node* number = loaded.node(1);
  const Graph::Node* box = loaded.node(2);
  const Graph::Node* boolean = loaded.node(3);
  ASSERT_NE(number, nullptr);
  ASSERT_NE(box, nullptr);
  ASSERT_NE(boolean, nullptr);

  // 位置 / 预览旗标 / 内联值 / 参数值。
  EXPECT_DOUBLE_EQ(box->x, 200.0);
  EXPECT_TRUE(box->preview);
  EXPECT_DOUBLE_EQ(box->input_values[1].as_number(), 1.5);
  EXPECT_DOUBLE_EQ(number->params[0].as_number(), 2.5);
  EXPECT_DOUBLE_EQ(number->params[2].as_number(), 7.0);
  EXPECT_EQ(boolean->params[0].as_text(), "cut");

  // 连线还原。
  const Link* link = loaded.link_into(2, 0);
  ASSERT_NE(link, nullptr);
  EXPECT_EQ(link->from_node, 1u);
  EXPECT_EQ(link->from_port, 0u);

  // 加载后全脏 + next_id 接续。
  EXPECT_TRUE(loaded.node(3)->dirty);
  EXPECT_EQ(loaded.next_id(), g.next_id());
}

TEST(NodeIo, UnknownTypeBecomesMissingNodeAndSurvivesRoundTrip) {
  Graph g;
  const NodeId id = g.add_node("future.type", 5, 6);
  g.node(id)->input_values.push_back(NodeValue::number(9.0));  // 按位保留的值
  auto bytes = write_node_graph(g);
  ASSERT_TRUE(bytes) << bytes.error();

  Graph loaded;
  ASSERT_TRUE(read_node_graph(loaded, *bytes));
  const Graph::Node* node = loaded.node(id);
  ASSERT_NE(node, nullptr);
  EXPECT_EQ(node->type, nullptr);  // 丢失节点
  EXPECT_EQ(node->type_id, "future.type");

  // 丢失节点再存一次，值不丢。
  auto bytes2 = write_node_graph(loaded);
  ASSERT_TRUE(bytes2) << bytes2.error();
  Graph loaded2;
  ASSERT_TRUE(read_node_graph(loaded2, *bytes2));
  ASSERT_EQ(loaded2.node(id)->input_values.size(), 1u);
  EXPECT_DOUBLE_EQ(loaded2.node(id)->input_values[0].as_number(), 9.0);
}

TEST(NodeIo, NewerVersionIsRejected) {
  Graph g;
  auto bytes = write_node_graph(g);
  ASSERT_TRUE(bytes) << bytes.error();
  // 手工把版本号改成未来的版本。
  ASSERT_GE(bytes->size(), 4u);
  (*bytes)[3] = 99;  // 小端版本号的高字节
  Graph loaded;
  auto ok = read_node_graph(loaded, *bytes);
  ASSERT_FALSE(ok);
  EXPECT_NE(ok.error().find("newer"), std::string::npos);
}

TEST(NodeIo, TruncatedDataIsRejected) {
  Graph g = make_sample_graph();
  auto bytes = write_node_graph(g);
  ASSERT_TRUE(bytes) << bytes.error();
  Graph loaded;
  auto ok = read_node_graph(loaded, std::span<const std::uint8_t>(*bytes).subspan(0, 6));
  ASSERT_FALSE(ok);
  // 半截档不往外漏：目标图仍是空的。
  EXPECT_TRUE(loaded.nodes().empty());
}

}  // namespace
}  // namespace tamias
