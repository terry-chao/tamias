#include "command/command_system.h"
#include "command/history.h"
#include "engine/io/binary_archive.h"
#include "engine/document/document_io.h"
#include "engine/io/mesh_io.h"
#include "engine/math/math.h"
#include "engine/document/picking.h"
#include "engine/modeling/occt_feature.h"
#include "engine/modeling/occt_shape_ops.h"
#include "engine/modeling/shape_ops.h"
#include "engine/render/render_runtime.h"

#include <gtest/gtest.h>

using namespace tamias;

namespace {

// 手动给文档加一个墙实体（绕过 OCCT 求值，供序列化/渲染测试）。
std::uint64_t add_wall_entity(Document& doc, Vec3 start, Vec3 end) {
  auto wall = std::make_unique<WallEntity>(WallEntity::drag(start, end, 0.2, 3.0));
  MeshAsset mesh_asset{};
  mesh_asset.name = wall->name;
  mesh_asset.cpu = make_demo_cube();
  auto& stored_mesh = doc.add_mesh(std::move(mesh_asset));
  wall->mesh_asset_id = stored_mesh.id;

  SceneNode node{};
  node.name = wall->name;
  node.mesh_asset_id = wall->mesh_asset_id;
  node.local_transform = wall->local_transform;
  SceneNode& stored_node = doc.scene().add_node(std::move(node));
  wall->id = stored_node.id;

  const std::uint64_t id = wall->id;
  doc.insert_entity(std::move(wall));
  doc.recompute_scene();
  return id;
}

}  // namespace

TEST(Math, AabbExpand) {
  Aabb box{};
  box.expand({1, 2, 3});
  box.expand({-1, 0, 4});
  EXPECT_FLOAT_EQ(box.min.x, -1.f);
  EXPECT_FLOAT_EQ(box.max.z, 4.f);
}

TEST(MeshIo, DemoCubeHasTriangles) {
  const auto mesh = make_demo_cube();
  EXPECT_FALSE(mesh.indices.empty());
  EXPECT_TRUE(mesh.bounds.valid());
}

TEST(MeshIo, SaveAndLoadObjRoundTrip) {
  const auto original = make_demo_cube();
  const auto path = std::filesystem::temp_directory_path() / "tamias_roundtrip.obj";
  ASSERT_TRUE(save_obj(path, original)) << "save failed";
  auto loaded = load_obj(path);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_EQ(loaded->indices.size(), original.indices.size());
  EXPECT_EQ(loaded->vertices.size(), original.vertices.size());
  EXPECT_TRUE(loaded->bounds.valid());
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(BinaryArchive, PodAndStringRoundTrip) {
  BinaryWriter w;
  ASSERT_TRUE(w.write_u8(0xAB));
  ASSERT_TRUE(w.write_u16(0xCDEF));
  ASSERT_TRUE(w.write_u32(0x12345678u));
  ASSERT_TRUE(w.write_u64(0x1122334455667788ull));
  ASSERT_TRUE(w.write_f32(3.5f));
  ASSERT_TRUE(w.write_bool(true));
  ASSERT_TRUE(w.write_string("tamias"));

  BinaryReader r(w.data());
  auto u8 = r.read_u8();
  ASSERT_TRUE(u8) << u8.error();
  EXPECT_EQ(*u8, 0xAB);
  auto u16 = r.read_u16();
  ASSERT_TRUE(u16);
  EXPECT_EQ(*u16, 0xCDEF);
  auto u32 = r.read_u32();
  ASSERT_TRUE(u32);
  EXPECT_EQ(*u32, 0x12345678u);
  auto u64 = r.read_u64();
  ASSERT_TRUE(u64);
  EXPECT_EQ(*u64, 0x1122334455667788ull);
  auto f = r.read_f32();
  ASSERT_TRUE(f);
  EXPECT_FLOAT_EQ(*f, 3.5f);
  auto b = r.read_bool();
  ASSERT_TRUE(b);
  EXPECT_TRUE(*b);
  auto s = r.read_string();
  ASSERT_TRUE(s);
  EXPECT_EQ(*s, "tamias");
}

TEST(DocumentIo, FileRoundTrip) {
  Document doc("roundtrip");
  MeshAsset asset{};
  asset.name = "cube";
  asset.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset));
  SceneNode node{};
  node.name = "cube-node";
  node.mesh_asset_id = stored.id;
  node.local_transform = translate({1.f, 2.f, 3.f});
  node.color = {0.2f, 0.4f, 0.6f};
  doc.scene().add_node(std::move(node));
  doc.recompute_scene();
  doc.mark_dirty();

  ViewportState viewport{};
  viewport.target = {1.f, 2.f, 3.f};
  viewport.distance = 12.f;
  viewport.yaw = 0.5f;
  viewport.pitch = 0.25f;
  viewport.render_mode = ViewRenderMode::Wireframe;

  const auto path = std::filesystem::temp_directory_path() / "tamias_roundtrip.tdoc";
  ASSERT_TRUE(save_document(path, doc, viewport)) << "save failed";
  auto loaded = load_document(path);
  ASSERT_TRUE(loaded) << loaded.error();
  EXPECT_EQ(loaded->document.name(), "roundtrip");
  EXPECT_EQ(loaded->document.meshes().size(), 1u);
  EXPECT_EQ(loaded->document.scene().nodes().size(), 1u);
  EXPECT_FALSE(loaded->document.dirty());
  ASSERT_TRUE(loaded->has_viewport);
  EXPECT_FLOAT_EQ(loaded->viewport.distance, 12.f);
  EXPECT_FLOAT_EQ(loaded->viewport.yaw, 0.5f);
  EXPECT_EQ(loaded->viewport.render_mode, ViewRenderMode::Wireframe);

  const auto& loaded_node = loaded->document.scene().nodes().front();
  EXPECT_EQ(loaded_node.name, "cube-node");
  EXPECT_EQ(loaded_node.mesh_asset_id, stored.id);
  EXPECT_FALSE(loaded_node.selected);
  EXPECT_FLOAT_EQ(loaded_node.color.x, 0.2f);
  EXPECT_FLOAT_EQ(loaded_node.local_transform(0, 3), 1.f);

  const auto* loaded_mesh = loaded->document.mesh(stored.id);
  ASSERT_NE(loaded_mesh, nullptr);
  EXPECT_EQ(loaded_mesh->cpu.indices.size(), stored.cpu.indices.size());
  EXPECT_EQ(loaded_mesh->cpu.vertices.size(), stored.cpu.vertices.size());

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(DocumentIo, EntityRoundTrip) {
  Document doc("entity");
  const std::uint64_t wall_id = add_wall_entity(doc, {0.f, 0.f, 0.f}, {0.f, 0.f, 5.f});

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();

  const Entity* re = restored->entity(wall_id);
  ASSERT_NE(re, nullptr);
  EXPECT_EQ(re->name, "wall");
  EXPECT_EQ(re->kind(), EntityKind::Wall);
  EXPECT_EQ(re->model.features().size(), 2u);
  EXPECT_EQ(re->model.features()[0].kind, FeatureKind::RectProfile);
  EXPECT_DOUBLE_EQ(re->model.param(re->model.features()[0].id, "width", 0.0), 0.2);
  EXPECT_EQ(re->model.features()[1].kind, FeatureKind::Extrude);
}

TEST(DocumentIo, EntityFileRoundTrip) {
  Document doc("entity-file");
  const std::uint64_t wall_id = add_wall_entity(doc, {0.f, 0.f, 0.f}, {0.f, 0.f, 5.f});

  ViewportState viewport{};
  const auto path = std::filesystem::temp_directory_path() / "tamias_entity_roundtrip.tdoc";
  ASSERT_TRUE(save_document(path, doc, viewport)) << "save failed";
  auto loaded = load_document(path);
  ASSERT_TRUE(loaded) << loaded.error();
  const Entity* re = loaded->document.entity(wall_id);
  ASSERT_NE(re, nullptr);
  EXPECT_EQ(re->kind(), EntityKind::Wall);
  EXPECT_EQ(re->model.features().size(), 2u);

  std::error_code ec;
  std::filesystem::remove(path, ec);
}

TEST(Document, RenderItemsAndSelection) {
  Document doc("render");
  add_wall_entity(doc, {0.f, 0.f, 0.f}, {0.f, 0.f, 5.f});
  add_wall_entity(doc, {2.f, 0.f, 0.f}, {2.f, 0.f, 5.f});

  auto items = doc.render_items();
  ASSERT_EQ(items.size(), 2u);

  const std::uint64_t first_id = items[0].node_id;
  doc.select(first_id);
  items = doc.render_items();
  bool found_selected = false;
  for (const auto& item : items) {
    if (item.node_id == first_id) {
      EXPECT_TRUE(item.selected);
      found_selected = true;
    }
  }
  EXPECT_TRUE(found_selected);
  ASSERT_NE(doc.selected_entity(), nullptr);
  EXPECT_EQ(doc.selected_entity()->id, first_id);
}

TEST(DocumentHistory, UndoRedoSnapshots) {
  Document doc("hist");
  MeshAsset asset{};
  asset.cpu = make_demo_cube();
  doc.add_mesh(std::move(asset));

  auto snap0 = serialize_document(doc);
  ASSERT_TRUE(snap0) << snap0.error();

  DocumentHistory history;
  history.reset_with(std::move(*snap0));

  doc.clear_content();
  MeshAsset asset2{};
  asset2.name = "second";
  asset2.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset2));
  SceneNode node{};
  node.name = "n2";
  node.mesh_asset_id = stored.id;
  doc.scene().add_node(std::move(node));

  auto snap1 = serialize_document(doc);
  ASSERT_TRUE(snap1);
  history.push_snapshot(std::move(*snap1));

  EXPECT_TRUE(history.can_undo());
  EXPECT_FALSE(history.can_redo());

  auto undone = history.undo();
  ASSERT_TRUE(undone) << undone.error();
  auto restored0 = deserialize_document(*undone);
  ASSERT_TRUE(restored0) << restored0.error();
  EXPECT_EQ(restored0->name(), "hist");
  EXPECT_EQ(restored0->meshes().size(), 1u);
  EXPECT_TRUE(restored0->scene().nodes().empty());

  auto redone = history.redo();
  ASSERT_TRUE(redone);
  auto restored1 = deserialize_document(*redone);
  ASSERT_TRUE(restored1);
  EXPECT_EQ(restored1->meshes().size(), 1u);
  EXPECT_EQ(restored1->scene().nodes().size(), 1u);
  EXPECT_EQ(restored1->scene().nodes().front().name, "n2");
}

TEST(Picking, RayHitsCube) {
  Document doc("t");
  MeshAsset asset{};
  asset.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset));
  SceneNode node{};
  node.mesh_asset_id = stored.id;
  doc.scene().add_node(std::move(node));
  doc.recompute_scene();

  Bvh bvh;
  bvh.build(doc);
  Ray ray{{0.f, 0.f, 5.f}, {0.f, 0.f, -1.f}};
  auto hit = bvh.closest_hit(ray, doc);
  ASSERT_TRUE(hit.has_value());
}

TEST(Modeling, MeshShapeTessellate) {
  MeshShape shape(make_demo_cube());
  auto mesh = shape.tessellate(0.1);
  ASSERT_TRUE(mesh.has_value());
  EXPECT_EQ(shape.backend_name(), "mesh");
}

TEST(RenderConfig, OpenGlDoesNotShare) {
  RenderDeviceConfig a{GraphicsBackend::OpenGL, true};
  RenderDeviceConfig b{GraphicsBackend::OpenGL, true};
  EXPECT_FALSE(a.shares_execution_thread_with(b));
  RenderDeviceConfig v0{GraphicsBackend::Vulkan, true};
  RenderDeviceConfig v1{GraphicsBackend::Vulkan, true};
  EXPECT_TRUE(v0.shares_execution_thread_with(v1));
}

#if defined(TAMIAS_HAS_OCCT)
TEST(Occt, TessellateBox) {
  auto mesh = tessellate_occt_box_for_tests();
  ASSERT_TRUE(mesh.has_value()) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
  EXPECT_TRUE(mesh->bounds.valid());
}

TEST(FeatureModel, ChangeParamReevaluates) {
  FeatureModel model;
  auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                    {{"width", 0.2}, {"height", 3.0}});
  const std::uint64_t profile_id = profile.id;
  model.add_feature(FeatureKind::Extrude, {profile_id}, {{"depth", 5.0}});

  auto mesh1 = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh1) << mesh1.error();
  EXPECT_FALSE(mesh1->indices.empty());
  EXPECT_TRUE(mesh1->bounds.valid());

  model.set_param(profile_id, "width", 0.4);
  auto mesh2 = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh2) << mesh2.error();
  // 墙变厚：X 方向 extent 增大。
  EXPECT_GT(mesh2->bounds.extent().x, mesh1->bounds.extent().x);
}

TEST(Entity, CreateGeom) {
  WallEntity wall = WallEntity::drag({0.f, 0.f, 0.f}, {0.f, 0.f, 5.f}, 0.2, 3.0);
  auto mesh = wall.createGeom();
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
  EXPECT_TRUE(mesh->bounds.valid());
}

TEST(CommandSystem, DispatchCreateWallUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("cmd");
  CommandArgs args = {{"start", Vec3{0.f, 0.f, 0.f}},
                      {"end", Vec3{0.f, 0.f, 5.f}},
                      {"thickness", 0.2},
                      {"height", 3.0}};
  auto r = system.dispatch(doc, "create_wall", args);
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(doc.entities().size(), 1u);
  EXPECT_EQ(doc.meshes().size(), 1u);

  ASSERT_TRUE(system.can_undo());
  system.undo();
  EXPECT_EQ(doc.entities().size(), 0u);
  EXPECT_TRUE(doc.meshes().empty());

  ASSERT_TRUE(system.can_redo());
  system.redo();
  EXPECT_EQ(doc.entities().size(), 1u);
  EXPECT_EQ(doc.meshes().size(), 1u);
}
#endif
