#include "engine/render/scene_graph.h"
#include "engine/document/document.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_set>

namespace tamias {
namespace {

// ---------------------------------------------------------------------------
// RHI mock：录制命令序列，供 RecordCommands 断言
// ---------------------------------------------------------------------------

class MockBuffer : public Buffer {
 public:
  const BufferDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  BufferDesc desc_{};
};

class MockTexture : public Texture {
 public:
  const TextureDesc& desc() const override { return desc_; }
  Result<void> write(std::uint64_t, std::span<const std::byte>) override { return {}; }
  TextureDesc desc_{};
};

class MockPipeline : public PipelineState {};

class MockCommandList : public CommandList {
 public:
  void begin() override {}
  void end() override {}
  void begin_render_pass(SwapChain&, const float[4], float) override {}
  void end_render_pass() override {}
  void set_pipeline(PipelineState& pipeline) override { pipelines.push_back(&pipeline); }
  void set_vertex_buffer(Buffer&, std::uint64_t) override { ++vertex_binds; }
  void set_index_buffer(Buffer&, std::uint64_t) override { ++index_binds; }
  void set_push_constants(std::span<const std::byte> data) override {
    PushConstants pc{};
    const std::size_t n = std::min(data.size(), sizeof(pc));
    std::memcpy(&pc, data.data(), n);
    push_constants.push_back(pc);
  }
  void set_texture(Texture&, std::uint32_t) override { ++texture_binds; }
  void draw_indexed(const DrawIndexedDesc& desc) override { draws.push_back(desc); }
  void set_viewport(float, float, float, float, float, float) override {}
  void set_scissor(std::int32_t, std::int32_t, std::uint32_t, std::uint32_t) override {}

  std::vector<PipelineState*> pipelines;
  std::vector<PushConstants> push_constants;
  std::vector<DrawIndexedDesc> draws;
  int texture_binds = 0;
  int vertex_binds = 0;
  int index_binds = 0;
};

// ---------------------------------------------------------------------------
// 测试脚手架
// ---------------------------------------------------------------------------

struct Fixture {
  MockCommandList cmds;
  MockPipeline shaded;
  MockPipeline wire;
  MockPipeline entity_line;
  MockTexture default_tex;
  std::unordered_map<std::uint64_t, GpuMesh> meshes;
  std::unordered_map<std::uint64_t, std::uint64_t> asset_to_gpu;
  std::unordered_map<std::uint64_t, GpuTexture> textures;
  std::unordered_map<std::uint64_t, std::uint64_t> texture_asset_to_gpu;
  bool texture_diag_logged = false;

  Fixture() {
    ctx.command_list = &cmds;
    ctx.view_proj = &view_proj;
    ctx.eye_position = {0.f, 0.f, 5.f};
    ctx.mode_value = 1.f;
    ctx.shaded_pipeline = &shaded;
    ctx.wire_pipeline = &wire;
    ctx.entity_line_pipeline = &entity_line;
    ctx.default_texture = &default_tex;
    ctx.meshes = &meshes;
    ctx.asset_to_gpu = &asset_to_gpu;
    ctx.textures = &textures;
    ctx.texture_asset_to_gpu = &texture_asset_to_gpu;
    ctx.texture_diag_logged = &texture_diag_logged;
  }

  void add_mesh(std::uint64_t asset_id, std::uint32_t index_count) {
    GpuMesh mesh;
    mesh.vertex_buffer = std::make_unique<MockBuffer>();
    mesh.index_buffer = std::make_unique<MockBuffer>();
    mesh.index_count = index_count;
    const std::uint64_t gpu_id = meshes.size() + 1;
    meshes.emplace(gpu_id, std::move(mesh));
    asset_to_gpu[asset_id] = gpu_id;
  }

  void visit(RenderNode& root) {
    RecordCommands visitor(ctx);
    root.accept(visitor);
  }

  Mat4 view_proj = Mat4::identity();
  SceneGraphDrawContext ctx;
};

std::unique_ptr<DrawableNode> make_drawable(std::uint64_t node_id, std::uint64_t mesh_asset_id) {
  auto drawable = std::make_unique<DrawableNode>();
  drawable->node_id = node_id;
  drawable->mesh_asset_id = mesh_asset_id;
  return drawable;
}

void expect_mat4_eq(const Mat4& a, const Mat4& b) {
  for (int i = 0; i < 16; ++i) {
    EXPECT_FLOAT_EQ(a.m[i], b.m[i]) << "matrix element m[" << i << "]";
  }
}

// ---------------------------------------------------------------------------
// 测试
// ---------------------------------------------------------------------------

TEST(SceneGraph, RecordsDrawWithAccumulatedTransformAndMaterial) {
  Fixture f;
  f.add_mesh(42, 6);

  auto root = std::make_unique<GroupNode>();
  auto transform = std::make_unique<TransformNode>();
  transform->matrix = translate({1.f, 2.f, 3.f});
  auto state = std::make_unique<StateGroupNode>();
  auto material = std::make_unique<BindMaterialCommand>();
  material->color = {1.f, 0.f, 0.f};
  material->roughness = 0.2f;
  material->metallic = 0.9f;
  state->commands.push_back(std::move(material));
  auto selected = std::make_unique<SetSelectedCommand>();
  selected->selected = true;
  state->commands.push_back(std::move(selected));
  state->add_child(make_drawable(7, 42));
  transform->add_child(std::move(state));
  root->add_child(std::move(transform));

  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), 1u);
  ASSERT_EQ(f.cmds.push_constants.size(), 1u);
  EXPECT_EQ(f.cmds.draws[0].index_count, 6u);
  EXPECT_EQ(f.cmds.texture_binds, 1);
  EXPECT_EQ(f.cmds.vertex_binds, 1);
  EXPECT_EQ(f.cmds.index_binds, 1);
  EXPECT_EQ(f.cmds.pipelines.back(), &f.shaded);

  const PushConstants& pc = f.cmds.push_constants[0];
  expect_mat4_eq(pc.model, translate({1.f, 2.f, 3.f}));
  expect_mat4_eq(pc.mvp, f.view_proj * translate({1.f, 2.f, 3.f}));
  EXPECT_FLOAT_EQ(pc.color[0], 1.f);
  EXPECT_FLOAT_EQ(pc.color[1], 0.f);
  EXPECT_FLOAT_EQ(pc.color[2], 0.f);
  EXPECT_FLOAT_EQ(pc.color[3], 1.f);
  EXPECT_FLOAT_EQ(pc.material[0], 0.2f);
  EXPECT_FLOAT_EQ(pc.material[1], 0.9f);
  EXPECT_FLOAT_EQ(pc.light_dir_selected[3], 1.f);  // selected
  EXPECT_FLOAT_EQ(pc.eye_pos_mode[3], 1.f);        // shaded mode
}

TEST(SceneGraph, NestedTransformsMultiplyInTraversalOrder) {
  Fixture f;
  f.add_mesh(1, 3);

  const Mat4 outer = translate({1.f, 0.f, 0.f});
  const Mat4 inner = translate({0.f, 2.f, 0.f});
  auto root = std::make_unique<GroupNode>();
  auto t1 = std::make_unique<TransformNode>();
  t1->matrix = outer;
  auto t2 = std::make_unique<TransformNode>();
  t2->matrix = inner;
  t2->add_child(make_drawable(1, 1));
  t1->add_child(std::move(t2));
  root->add_child(std::move(t1));

  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), 1u);
  expect_mat4_eq(f.cmds.push_constants[0].model, outer * inner);
  expect_mat4_eq(f.cmds.push_constants[0].mvp, f.view_proj * outer * inner);
}

TEST(SceneGraph, InvisibleSubtreeIsSkipped) {
  Fixture f;
  f.add_mesh(1, 3);

  auto root = std::make_unique<GroupNode>();
  auto transform = std::make_unique<TransformNode>();
  transform->visible = false;
  transform->add_child(make_drawable(1, 1));
  root->add_child(std::move(transform));

  f.visit(*root);

  EXPECT_TRUE(f.cmds.draws.empty());
  EXPECT_TRUE(f.cmds.push_constants.empty());
}

TEST(SceneGraph, SiblingStateGroupsOverrideExplicitly) {
  Fixture f;
  f.add_mesh(10, 3);
  f.add_mesh(11, 3);

  auto root = std::make_unique<GroupNode>();

  auto make_item = [&](Vec3 color) {
    auto transform = std::make_unique<TransformNode>();
    auto state = std::make_unique<StateGroupNode>();
    auto material = std::make_unique<BindMaterialCommand>();
    material->color = color;
    state->commands.push_back(std::move(material));
    state->add_child(make_drawable(0, 10));
    transform->add_child(std::move(state));
    root->add_child(std::move(transform));
  };
  make_item({1.f, 0.f, 0.f});
  make_item({0.f, 0.f, 1.f});

  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), 2u);
  ASSERT_EQ(f.cmds.push_constants.size(), 2u);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].color[0], 1.f);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[1].color[2], 1.f);
}

TEST(SceneGraph, LinesCommandSelectsEntityLinePipelineAndMode3) {
  Fixture f;
  f.add_mesh(5, 2);

  auto root = std::make_unique<GroupNode>();
  auto state = std::make_unique<StateGroupNode>();
  auto lines = std::make_unique<SetLinesCommand>();
  lines->lines = true;
  state->commands.push_back(std::move(lines));
  state->add_child(make_drawable(3, 5));
  root->add_child(std::move(state));

  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), 1u);
  EXPECT_EQ(f.cmds.pipelines.back(), &f.entity_line);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].eye_pos_mode[3], 3.f);
}

TEST(SceneGraph, MissingMeshAssetSkipsDrawWithoutCrashing) {
  Fixture f;
  f.add_mesh(1, 3);

  auto root = std::make_unique<GroupNode>();
  root->add_child(make_drawable(9, 999));

  f.visit(*root);

  EXPECT_TRUE(f.cmds.draws.empty());
}

TEST(SceneGraph, BuildFromItemsMapsFieldsAndTreeShape) {
  Fixture f;
  f.add_mesh(42, 6);
  f.add_mesh(43, 12);

  const Mat4 world = translate({10.f, 0.f, 0.f});
  std::vector<SceneDrawItem> items;
  SceneDrawItem a{};
  a.node_id = 1;
  a.mesh_asset_id = 42;
  a.transform = world;
  a.color = {1.f, 0.f, 0.f};
  a.roughness = 0.3f;
  a.metallic = 0.4f;
  a.selected = true;
  items.push_back(a);
  SceneDrawItem b{};
  b.node_id = 2;
  b.mesh_asset_id = 43;
  b.color = {0.f, 0.f, 1.f};
  items.push_back(b);

  auto root = build_scene_graph(items);
  auto* group = dynamic_cast<GroupNode*>(root.get());
  ASSERT_NE(group, nullptr);
  ASSERT_EQ(group->children.size(), 2u);

  auto* transform = dynamic_cast<TransformNode*>(group->children[0].get());
  ASSERT_NE(transform, nullptr);
  expect_mat4_eq(transform->matrix, world);
  ASSERT_EQ(transform->children.size(), 1u);
  auto* state = dynamic_cast<StateGroupNode*>(transform->children[0].get());
  ASSERT_NE(state, nullptr);
  EXPECT_EQ(state->commands.size(), 3u);  // material / selected / lines
  ASSERT_EQ(state->children.size(), 1u);
  auto* drawable = dynamic_cast<DrawableNode*>(state->children[0].get());
  ASSERT_NE(drawable, nullptr);
  EXPECT_EQ(drawable->node_id, 1u);
  EXPECT_EQ(drawable->mesh_asset_id, 42u);

  f.visit(*root);

  ASSERT_EQ(f.cmds.draws.size(), 2u);
  ASSERT_EQ(f.cmds.push_constants.size(), 2u);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].color[0], 1.f);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].light_dir_selected[3], 1.f);
  expect_mat4_eq(f.cmds.push_constants[0].model, world);
  EXPECT_EQ(f.cmds.draws[0].index_count, 6u);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[1].color[2], 1.f);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[1].light_dir_selected[3], 0.f);
  EXPECT_EQ(f.cmds.draws[1].index_count, 12u);
  EXPECT_TRUE(f.texture_diag_logged);
}

// ---------------------------------------------------------------------------
// Scene 脏标记
// ---------------------------------------------------------------------------

TEST(SceneDirty, SetTransformMarksSubtreeAndBumpsGeneration) {
  Scene scene;
  SceneNode parent{};
  parent.name = "parent";
  const std::uint64_t pid = scene.add_node(std::move(parent)).id;
  SceneNode child{};
  child.name = "child";
  const std::uint64_t cid = scene.add_node(std::move(child)).id;
  scene.set_parent(cid, pid);
  const std::uint64_t gen_before = scene.generation();

  scene.set_transform(pid, translate({1.f, 0.f, 0.f}));

  EXPECT_GT(scene.generation(), gen_before);
  const auto dirty = scene.dirty_since(gen_before);
  EXPECT_NE(std::find(dirty.begin(), dirty.end(), pid), dirty.end());
  EXPECT_NE(std::find(dirty.begin(), dirty.end(), cid), dirty.end());
}

TEST(SceneDirty, SelectionAndMaterialTouchesOnlyNode) {
  Document doc;
  Scene& scene = doc.scene();
  const std::uint64_t id = scene.add_node(SceneNode{}).id;
  const std::uint64_t gen = scene.generation();

  doc.select(id);
  auto dirty = scene.dirty_since(gen);
  ASSERT_EQ(dirty.size(), 1u);
  EXPECT_EQ(dirty[0], id);

  const std::uint64_t gen2 = scene.generation();
  doc.mark_scene_dirty(id);
  dirty = scene.dirty_since(gen2);
  ASSERT_EQ(dirty.size(), 1u);
  EXPECT_EQ(dirty[0], id);
}

TEST(SceneDirty, AddAndRemoveMarkIds) {
  Scene scene;
  const std::uint64_t g0 = scene.generation();
  const std::uint64_t id = scene.add_node(SceneNode{}).id;
  const std::uint64_t g1 = scene.generation();

  auto dirty = scene.dirty_since(g0);
  ASSERT_EQ(dirty.size(), 1u);
  EXPECT_EQ(dirty[0], id);

  scene.remove_node(id);
  dirty = scene.dirty_since(g1);
  ASSERT_EQ(dirty.size(), 1u);
  EXPECT_EQ(dirty[0], id);
}

TEST(SceneDirty, DirtySinceOnlyReturnsNewerGenerations) {
  Scene scene;
  const std::uint64_t a = scene.add_node(SceneNode{}).id;
  const std::uint64_t g1 = scene.generation();
  const std::uint64_t b = scene.add_node(SceneNode{}).id;
  const std::uint64_t g2 = scene.generation();

  auto all = scene.dirty_since(0);
  ASSERT_EQ(all.size(), 2u);
  EXPECT_NE(std::find(all.begin(), all.end(), a), all.end());
  EXPECT_NE(std::find(all.begin(), all.end(), b), all.end());

  auto dirty = scene.dirty_since(g1);
  ASSERT_EQ(dirty.size(), 1u);
  EXPECT_EQ(dirty[0], b);

  dirty = scene.dirty_since(g2);
  EXPECT_TRUE(dirty.empty());
}

TEST(SceneDirty, ClearResetsGenerationAndHistory) {
  Scene scene;
  scene.add_node(SceneNode{});
  scene.set_transform(scene.nodes().front().id, translate({2.f, 0.f, 0.f}));
  EXPECT_GT(scene.generation(), 1u);

  scene.clear();

  EXPECT_EQ(scene.generation(), 1u);
  EXPECT_TRUE(scene.dirty_since(0).empty());
}

// ---------------------------------------------------------------------------
// 渲染树增量同步
// ---------------------------------------------------------------------------

TEST(SceneGraphIncremental, UpdatesExistingNodeInPlace) {
  Fixture f;
  f.add_mesh(10, 3);
  f.add_mesh(11, 3);

  std::vector<SceneDrawItem> items;
  SceneDrawItem a{};
  a.node_id = 1;
  a.mesh_asset_id = 10;
  a.transform = translate({1.f, 0.f, 0.f});
  a.color = {1.f, 0.f, 0.f};
  items.push_back(a);
  SceneDrawItem b{};
  b.node_id = 2;
  b.mesh_asset_id = 11;
  b.color = {0.f, 1.f, 0.f};
  items.push_back(b);

  std::unordered_map<std::uint64_t, TransformNode*> index;
  auto root = build_scene_graph(items, &index);
  auto* group = dynamic_cast<GroupNode*>(root.get());
  ASSERT_NE(group, nullptr);
  ASSERT_EQ(index.size(), 2u);
  TransformNode* original_wrapper = index[1];

  // 只改 node 1（直接改 items[0]，不是局部副本 a）：换世界矩阵 + 颜色 + 选中态。
  items[0].transform = translate({9.f, 8.f, 7.f});
  items[0].color = {0.f, 0.f, 1.f};
  items[0].selected = true;
  update_scene_graph(*group, index, items, {1});

  EXPECT_EQ(index.size(), 2u);
  EXPECT_EQ(index[1], original_wrapper);  // 节点原地更新，不重建
  EXPECT_EQ(group->children.size(), 2u);

  f.visit(*root);
  ASSERT_EQ(f.cmds.draws.size(), 2u);
  ASSERT_EQ(f.cmds.push_constants.size(), 2u);
  expect_mat4_eq(f.cmds.push_constants[0].model, translate({9.f, 8.f, 7.f}));
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].color[2], 1.f);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[0].light_dir_selected[3], 1.f);
  // node 2 保持原样。
  expect_mat4_eq(f.cmds.push_constants[1].model, Mat4::identity());
  EXPECT_FLOAT_EQ(f.cmds.push_constants[1].color[1], 1.f);
  EXPECT_FLOAT_EQ(f.cmds.push_constants[1].light_dir_selected[3], 0.f);
}

TEST(SceneGraphIncremental, InsertAndRemoveSubtrees) {
  Fixture f;
  f.add_mesh(10, 3);
  f.add_mesh(11, 3);
  f.add_mesh(12, 3);

  auto make_item = [](std::uint64_t id, std::uint64_t mesh) {
    SceneDrawItem item{};
    item.node_id = id;
    item.mesh_asset_id = mesh;
    return item;
  };

  std::unordered_map<std::uint64_t, TransformNode*> index;
  auto root = build_scene_graph({make_item(1, 10), make_item(2, 11)}, &index);
  auto* group = dynamic_cast<GroupNode*>(root.get());
  ASSERT_NE(group, nullptr);

  // 删除 node 2。
  update_scene_graph(*group, index, {make_item(1, 10)}, {2});
  ASSERT_EQ(group->children.size(), 1u);
  EXPECT_EQ(index.size(), 1u);
  EXPECT_EQ(index.count(2), 0u);

  // 新增 node 3。
  update_scene_graph(*group, index, {make_item(1, 10), make_item(3, 12)}, {3});
  ASSERT_EQ(group->children.size(), 2u);
  ASSERT_EQ(index.size(), 2u);
  EXPECT_NE(index.find(3), index.end());

  f.visit(*root);
  ASSERT_EQ(f.cmds.draws.size(), 2u);
  EXPECT_EQ(f.cmds.draws[0].index_count, 3u);
  EXPECT_EQ(f.cmds.draws[1].index_count, 3u);
}

TEST(SceneGraphIncremental, RecordCullsByFrustumAndHiddenSet) {
  Fixture f;
  f.add_mesh(10, 3);

  SceneDrawItem item{};
  item.node_id = 1;
  item.mesh_asset_id = 10;
  item.bounds = Aabb{{-1.f, -1.f, -1.f}, {1.f, 1.f, 1.f}};
  auto root = build_scene_graph({item});

  // 视锥不包含包围盒 → 剔除。
  Frustum far_frustum = Frustum::from_view_proj(
      perspective(1.0f, 1.0f, 0.1f, 10.f) *
      look_at({0.f, 0.f, 100.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}));
  f.ctx.frustum = &far_frustum;
  f.visit(*root);
  EXPECT_TRUE(f.cmds.draws.empty());
  f.cmds.draws.clear();

  // 视锥包含 → 画；但隐藏集命中 → 不画。
  f.ctx.frustum = nullptr;
  std::unordered_set<std::uint64_t> hidden{1};
  f.ctx.hidden_nodes = &hidden;
  f.visit(*root);
  EXPECT_TRUE(f.cmds.draws.empty());
  f.cmds.draws.clear();

  hidden.clear();
  f.visit(*root);
  ASSERT_EQ(f.cmds.draws.size(), 1u);
}

}  // namespace
}  // namespace tamias
