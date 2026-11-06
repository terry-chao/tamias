#include "engine/modeling/nodes/node_type.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace tamias {
namespace {

const NodeRegistry& registry() { return NodeRegistry::builtin(); }

TEST(NodePaletteSearch, FindsExactAndPrefixOverSubsequence) {
  auto hits = registry().search("Box");
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits[0].type->id, "solid.box");  // 精确命中排最前

  auto sub = registry().search("bx");
  ASSERT_FALSE(sub.empty());
  // 子序列也能搜到 box（b…x），但分数低于精确。
  const bool has_box = std::any_of(sub.begin(), sub.end(), [](const auto& h) {
    return h.type->id == "solid.box";
  });
  EXPECT_TRUE(has_box);
}

TEST(NodePaletteSearch, ChineseKeywordsHit) {
  auto hits = registry().search("圆柱");
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits[0].type->id, "solid.cylinder");
}

TEST(NodePaletteSearch, DotSuffixOfIdMatches) {
  auto hits = registry().search("boolean");
  ASSERT_FALSE(hits.empty());
  EXPECT_EQ(hits[0].type->id, "solid.boolean");
}

TEST(NodePaletteSearch, UnknownQueryReturnsEmpty) {
  EXPECT_TRUE(registry().search("zzzznope").empty());
}

TEST(NodePaletteSearch, OrderedIsCategoryGrouped) {
  const auto& ordered = registry().ordered();
  ASSERT_GE(ordered.size(), 9u);
  int last_major = -1;
  for (const NodeTypeInfo* type : ordered) {
    const int major = node_category_order(type->category);
    EXPECT_GE(major, last_major);  // 大类单调不减
    last_major = major;
  }
}

TEST(NodePaletteSearch, PortSearchPrefersCompatibleTypes) {
  // 从 Body 输出拖到空白：有 Body 输入端口的节点优先。
  auto hits = registry().search_for_port("", PortType::Body, false);
  ASSERT_GE(hits.size(), 2u);
  const NodeTypeInfo* first = hits[0].type;
  bool first_accepts_body = false;
  for (const PortSpec& spec : first->inputs) {
    if (spec.type == PortType::Body) {
      first_accepts_body = true;
    }
  }
  EXPECT_TRUE(first_accepts_body);
  // input.number 不该排在前面。
  EXPECT_NE(hits[0].type->id, "input.number");
}

TEST(NodePaletteSearch, PortSearchAsInputRanksBodySourcesFirst) {
  // 往 Body 输入端口上连：能产出 Body 的节点优先。
  auto hits = registry().search_for_port("", PortType::Body, true);
  ASSERT_FALSE(hits.empty());
  EXPECT_NE(hits[0].type->outputs.empty(), true);
  EXPECT_EQ(hits[0].type->outputs[0].type, PortType::Body);
}

}  // namespace
}  // namespace tamias
