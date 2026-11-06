#include "engine/document/document.h"
#include "engine/modeling/linked_kernels.h"
#include "engine/modeling/mki/kernel.h"
#include "engine/modeling/nodes/node_eval.h"
#include "engine/modeling/nodes/node_graph.h"

#include <gtest/gtest.h>

#include <memory>

namespace tamias {
namespace {

// 预览管线的端到端检查：新加的实体节点 → 求值出 Body → 细分进文档 →
// render_items 末尾多一条哨兵段（1<<62）的 draw item。
// 这一步挂了，节点编辑器里「加节点没反应」就是它。
TEST(NodePreview, BodyOutputBecomesRenderItem) {
  register_linked_kernels();
  auto created = ModelKernel::create(KernelCreateInfo{default_kernel_backend()});
  ASSERT_TRUE(created) << created.error();
  std::unique_ptr<ModelKernel> kernel = std::move(*created);

  Document document;
  Graph& graph = document.node_graph();
  const NodeId box = graph.add_node("solid.box", 0, 0);
  ASSERT_NE(graph.node(box), nullptr);
  // 加节点默认开预览，不然用户加完什么都看不到。
  ASSERT_TRUE(graph.node(box)->preview);

  SolveContext context;
  context.kernel = kernel.get();
  ASSERT_TRUE(evaluate_graph(graph, context));

  const BodyRef body = graph.node(box)->outputs[0].as_body();
  ASSERT_NE(body, nullptr);

  auto tessellated = kernel->tessellate(*body, 0.05);
  ASSERT_TRUE(tessellated) << tessellated.error();
  MeshAsset& mesh = document.intern_mesh("node_preview", std::move(*tessellated));

  NodePreviewEntry entry;
  entry.mesh_asset_id = mesh.id;
  document.set_node_preview({entry});

  const std::vector<SceneDrawItem> items = document.render_items();
  ASSERT_EQ(items.size(), 1u);
  EXPECT_GE(items[0].node_id, 1ull << 62);  // kNodePreviewNodeIdBase 哨兵段
  EXPECT_EQ(items[0].mesh_asset_id, mesh.id);
  EXPECT_NE(document.mesh(mesh.id), nullptr);
  // 预览不是实体：不进 Scene、不可选。
  EXPECT_TRUE(document.scene().nodes().empty());
}

TEST(NodePreview, ClearingPreviewDropsRenderItem) {
  Document document;
  Graph& graph = document.node_graph();
  const NodeId box = graph.add_node("solid.box", 0, 0);
  ASSERT_NE(graph.node(box), nullptr);
  NodePreviewEntry entry;
  entry.mesh_asset_id = 1;  // 不存在的资产：render_items 里自然被跳过
  document.set_node_preview({entry});
  EXPECT_TRUE(document.render_items().empty());
  document.clear_node_preview();
  EXPECT_TRUE(document.render_items().empty());
}

}  // namespace
}  // namespace tamias
