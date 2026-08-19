#include "bim/host_geometry.h"
#include "bim/host_update.h"
#include "command/command_system.h"
#include "command/history.h"
#include "engine/io/binary_archive.h"
#include "engine/document/document_io.h"
#include "entity/beam_entity.h"
#include "entity/bezier_entity.h"
#include "entity/box_entity.h"
#include "entity/circle_entity.h"
#include "entity/column_entity.h"
#include "entity/door_entity.h"
#include "entity/family_entity.h"
#include "entity/line_entity.h"
#include "entity/polyline_entity.h"
#include "entity/rectangle_entity.h"
#include "entity/sketch_entity.h"
#include "entity/slab_entity.h"
#include "entity/wall_entity.h"
#include "entity/window_entity.h"
#include "engine/modeling/curve_geom.h"
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
  auto wall = std::make_unique<WallEntity>(start, end, 0.2, 3.0);
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

bool contains_mesh(const std::vector<SceneDrawItem>& items, std::uint64_t mesh_asset_id) {
  for (const auto& item : items) {
    if (item.mesh_asset_id == mesh_asset_id) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST(Math, AabbExpand) {
  Aabb box{};
  box.expand({1, 2, 3});
  box.expand({-1, 0, 4});
  EXPECT_FLOAT_EQ(box.min.x, -1.f);
  EXPECT_FLOAT_EQ(box.max.z, 4.f);
}

TEST(Math, FrustumCullsAabb) {
  // Eye at +Z looking at origin (OpenGL camera space, −Z forward).
  const Mat4 view = look_at({0.f, 0.f, 8.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
  const Mat4 proj = perspective(0.8f, 1.f, 0.05f, 500.f);
  const Frustum frustum = Frustum::from_view_proj(proj * view);

  Aabb in_front{};
  in_front.expand({-1.f, -1.f, -1.f});
  in_front.expand({1.f, 1.f, 1.f});
  EXPECT_TRUE(frustum.intersects(in_front));

  Aabb behind{};
  behind.expand({-1.f, -1.f, 40.f});
  behind.expand({1.f, 1.f, 42.f});
  EXPECT_FALSE(frustum.intersects(behind));

  Aabb beside{};
  beside.expand({99.f, -1.f, -1.f});
  beside.expand({101.f, 1.f, 1.f});
  EXPECT_FALSE(frustum.intersects(beside));

  // Straddles the left/right edge of the pyramid → keep (conservative).
  Aabb grazing{};
  grazing.expand({2.5f, -1.f, -1.f});
  grazing.expand({4.5f, 1.f, 1.f});
  EXPECT_TRUE(frustum.intersects(grazing));

  EXPECT_TRUE(frustum.intersects(Aabb{}));
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

TEST(Document, RenderItemsFrustumCullsOffscreen) {
  Document doc("frustum");
  const std::uint64_t front =
      doc.add_import_mesh("front", make_demo_cube(), Mat4::identity(), {1.f, 1.f, 1.f});
  const std::uint64_t behind =
      doc.add_import_mesh("behind", make_demo_cube(), translate({0.f, 0.f, 40.f}), {1.f, 1.f, 1.f});
  const std::uint64_t beside =
      doc.add_import_mesh("beside", make_demo_cube(), translate({80.f, 0.f, 0.f}), {1.f, 1.f, 1.f});

  const auto all = doc.render_items();
  ASSERT_EQ(all.size(), 3u);
  EXPECT_TRUE(contains_mesh(all, front));
  EXPECT_TRUE(contains_mesh(all, behind));
  EXPECT_TRUE(contains_mesh(all, beside));

  const Mat4 view = look_at({0.f, 0.f, 8.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
  const Mat4 proj = perspective(0.8f, 1.f, 0.05f, 500.f);
  const Frustum frustum = Frustum::from_view_proj(proj * view);
  const auto visible = doc.render_items(&frustum);
  ASSERT_EQ(visible.size(), 1u);
  EXPECT_TRUE(contains_mesh(visible, front));
  EXPECT_FALSE(contains_mesh(visible, behind));
  EXPECT_FALSE(contains_mesh(visible, beside));

  EXPECT_EQ(doc.render_items(nullptr).size(), 3u);
}

TEST(Document, RenderItemsKeepsInvalidBounds) {
  Document doc("invalid-bounds");
  MeshAsset asset{};
  asset.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset));
  SceneNode node{};
  node.name = "unbounded";
  node.mesh_asset_id = stored.id;
  const std::uint64_t id = doc.scene().add_node(std::move(node)).id;

  const Mat4 view = look_at({0.f, 0.f, 8.f}, {0.f, 0.f, 0.f}, {0.f, 1.f, 0.f});
  const Mat4 proj = perspective(0.8f, 1.f, 0.05f, 500.f);
  const Frustum frustum = Frustum::from_view_proj(proj * view);
  const auto items = doc.render_items(&frustum);
  ASSERT_EQ(items.size(), 1u);
  EXPECT_EQ(items[0].node_id, id);
}

TEST(Document, DefaultMaterialsHaveAlbedoTextures) {
  Document doc("materials");
  ASSERT_EQ(doc.textures().size(), 6u);
  for (const auto& [id, material] : doc.materials()) {
    (void)id;
    EXPECT_NE(material.albedo_texture_id, 0u);
    EXPECT_NE(doc.texture(material.albedo_texture_id), nullptr);
  }
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

TEST(Picking, RayHitsTransformedNode) {
  Document doc("t");
  MeshAsset asset{};
  asset.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(asset));
  SceneNode node{};
  node.mesh_asset_id = stored.id;
  // 非单位变换（平移 + 绕 Y 旋转）：模拟墙体的放置。旧实现在三角形相交时忽略
  // 此变换、直接用世界射线打局部顶点，导致带 transform 的节点永远选不中。
  node.local_transform = translate({2.f, 0.f, 3.f}) * rotate_y(1.2f);
  SceneNode& placed = doc.scene().add_node(std::move(node));
  doc.recompute_scene();

  Bvh bvh;
  bvh.build(doc);
  // 从正上方垂直打下，穿过节点世界包围盒中心。
  const Vec3 center = placed.world_bounds.center();
  Ray ray{center + Vec3{0.f, 5.f, 0.f}, {0.f, -1.f, 0.f}};
  auto hit = bvh.closest_hit(ray, doc);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->node_id, placed.id);
}

TEST(Picking, RayHitsSketchLine) {
  Document doc("line");
  auto entity = std::make_unique<LineEntity>(Vec3{0.f, 0.f, 0.f}, Vec3{2.f, 0.f, 0.f});
  auto geom = entity->createGeom();
  ASSERT_TRUE(geom) << geom.error();
  Entity* added = doc.add_entity(std::move(entity), std::move(*geom));
  ASSERT_NE(added, nullptr);
  doc.recompute_scene();

  Bvh bvh;
  bvh.build(doc);
  Ray ray{{1.f, 1.f, 0.f}, {0.f, -1.f, 0.f}};
  auto hit = bvh.closest_hit(ray, doc);
  ASSERT_TRUE(hit.has_value());
  EXPECT_EQ(hit->node_id, added->id);
}

TEST(Math, IntersectSegmentHitsNearbyRay) {
  Ray ray{{1.f, 1.f, 0.f}, {0.f, -1.f, 0.f}};
  float t = 0.f;
  ASSERT_TRUE(intersect_segment(ray, {0.f, 0.f, 0.f}, {2.f, 0.f, 0.f}, 0.03f, t));
  EXPECT_NEAR(t, 1.f, 1e-4f);
  EXPECT_FALSE(intersect_segment(ray, {0.f, 0.f, 1.f}, {2.f, 0.f, 1.f}, 0.03f, t));
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
  WallEntity wall({0.f, 0.f, 0.f}, {0.f, 0.f, 5.f}, 0.2, 3.0);
  auto mesh = wall.createGeom();
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
  EXPECT_TRUE(mesh->bounds.valid());

  BoxEntity box({0.f, 0.f, 0.f});
  EXPECT_TRUE(dynamic_cast<const FamilyEntity*>(&wall) != nullptr);
  EXPECT_FALSE(dynamic_cast<const FamilyEntity*>(&box) != nullptr);
}

TEST(SketchEntity, CreateGeomAndNotFamily) {
  LineEntity line({0.f, 0.f, 0.f}, {2.f, 0.f, 0.f});
  auto line_mesh = line.createGeom();
  ASSERT_TRUE(line_mesh) << line_mesh.error();
  EXPECT_TRUE(line_mesh->line_list);
  EXPECT_EQ(line_mesh->vertices.size(), 2u);
  EXPECT_EQ(line_mesh->indices.size(), 2u);
  EXPECT_TRUE(line.is_sketch_entity());
  EXPECT_FALSE(line.is_family_entity());

  CircleEntity circle({0.f, 0.f, 0.f}, 1.0);
  auto circle_mesh = circle.createGeom();
  ASSERT_TRUE(circle_mesh) << circle_mesh.error();
  EXPECT_TRUE(circle_mesh->line_list);
  EXPECT_FALSE(circle_mesh->indices.empty());
  EXPECT_TRUE(circle.is_sketch_entity());

  PolylineEntity poly({{0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, {1.f, 0.f, 1.f}});
  auto poly_mesh = poly.createGeom();
  ASSERT_TRUE(poly_mesh) << poly_mesh.error();
  EXPECT_TRUE(poly_mesh->line_list);
  EXPECT_EQ(poly_mesh->vertices.size(), 3u);
  EXPECT_EQ(poly_mesh->indices.size(), 4u);

  RectangleEntity rect({0.f, 0.f, 0.f}, {2.f, 0.f, 1.f});
  auto rect_mesh = rect.createGeom();
  ASSERT_TRUE(rect_mesh) << rect_mesh.error();
  EXPECT_TRUE(rect_mesh->line_list);
  EXPECT_EQ(rect_mesh->vertices.size(), 5u);

  BezierEntity bezier({0.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, {2.f, 0.f, 1.f}, {2.f, 0.f, 0.f});
  auto bezier_mesh = bezier.createGeom();
  ASSERT_TRUE(bezier_mesh) << bezier_mesh.error();
  EXPECT_TRUE(bezier_mesh->line_list);
}

TEST(CurveGeom, ArcPassesThroughMidPoint) {
  const Vec3 start{1.f, 0.f, 0.f};
  const Vec3 through{0.f, 0.f, 1.f};
  const Vec3 end{-1.f, 0.f, 0.f};
  const auto pts = sample_arc_3pt(start, through, end, 48);
  ASSERT_GE(pts.size(), 3u);
  float best = 1e9f;
  for (const Vec3& p : pts) {
    best = std::min(best, length(p - through));
  }
  EXPECT_LT(best, 0.05f);
}

TEST(Entity, ParametricGeometryHasNormals) {
  BoxEntity box({0.f, 0.f, 0.f});
  auto box_mesh = box.createGeom();
  ASSERT_TRUE(box_mesh) << box_mesh.error();
  for (const auto& v : box_mesh->vertices) {
    EXPECT_GT(length(v.normal), 0.9f);
  }

  WallEntity wall({0.f, 0.f, 0.f}, {0.f, 0.f, 5.f}, 0.2, 3.0);
  auto wall_mesh = wall.createGeom();
  ASSERT_TRUE(wall_mesh) << wall_mesh.error();
  for (const auto& v : wall_mesh->vertices) {
    EXPECT_GT(length(v.normal), 0.9f);
  }
}

TEST(Entity, BuildingComponentsCreateGeom) {
  const BeamEntity beam({0.f, 0.f, 0.f}, {4.f, 0.f, 0.f}, 0.3, 0.5);
  const ColumnEntity column({0.f, 0.f, 0.f}, 0.4, 0.4, 3.0);
  const SlabEntity slab({0.f, 0.f, 0.f}, 4.0, 3.0, 0.2);
  const DoorEntity door({0.f, 0.f, 0.f}, 1.0, 2.1, 0.05);
  const WindowEntity window({0.f, 0.f, 0.f}, 1.2, 1.2, 0.08);

  for (const Entity* e : {static_cast<const Entity*>(&beam),
                          static_cast<const Entity*>(&column),
                          static_cast<const Entity*>(&slab),
                          static_cast<const Entity*>(&door),
                          static_cast<const Entity*>(&window)}) {
    auto mesh = e->createGeom();
    ASSERT_TRUE(mesh) << mesh.error();
    EXPECT_FALSE(mesh->indices.empty());
    EXPECT_TRUE(mesh->bounds.valid());
  }
}

TEST(CommandSystem, DispatchCreateWallUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("cmd");
  // 交互式命令：dispatch 只是武装，喂两个点后才创建。
  CommandArgs args = {{"thickness", 0.2}, {"height", 3.0}};
  auto r = system.dispatch(doc, "create_wall", args);
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(doc.entities().size(), 0u);  // pending，还没创建

  auto p1 = system.feed_point({0.f, 0.f, 0.f});  // 第一点
  ASSERT_TRUE(p1) << p1.error();
  EXPECT_FALSE(*p1);  // 还没完
  EXPECT_EQ(doc.entities().size(), 0u);

  auto p2 = system.feed_point({0.f, 0.f, 5.f});  // 第二点
  ASSERT_TRUE(p2) << p2.error();
  EXPECT_TRUE(*p2);  // 完成
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

TEST(CommandSystem, CreateCircleTwoClicks) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("sketch-circle");
  ASSERT_TRUE(system.dispatch(doc, "create_circle", {}));
  auto p1 = system.feed_point({0.f, 0.f, 0.f});
  ASSERT_TRUE(p1) << p1.error();
  EXPECT_FALSE(*p1);
  auto p2 = system.feed_point({1.f, 0.f, 0.f});
  ASSERT_TRUE(p2) << p2.error();
  EXPECT_TRUE(*p2);
  ASSERT_EQ(doc.entities().size(), 1u);
  EXPECT_EQ(doc.entities().begin()->second->kind(), EntityKind::Circle);
}

TEST(CommandSystem, CreateBezierClicksThenConfirm) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("sketch-bezier");
  ASSERT_TRUE(system.dispatch(doc, "create_bezier", {}));

  const Vec3 p0{0.f, 0.f, 0.f};
  const Vec3 p1{0.f, 0.f, 1.f};
  const Vec3 p2{2.f, 0.f, 1.f};
  const Vec3 p3{2.f, 0.f, 0.f};
  const Vec3 p4{3.f, 0.f, 0.5f};
  const Vec3 cursor{4.f, 0.f, 0.f};

  auto click0 = system.feed_point(p0);
  ASSERT_TRUE(click0) << click0.error();
  EXPECT_FALSE(*click0);
  {
    const auto controls = system.preview_control_polyline(cursor);
    ASSERT_EQ(controls.size(), 2u);
    EXPECT_EQ(system.preview_points(cursor).size(), 2u);
    EXPECT_GE(system.preview_polyline(cursor).size(), 2u);
    auto too_soon = system.confirm();
    ASSERT_TRUE(too_soon) << too_soon.error();
    EXPECT_FALSE(*too_soon);
  }

  auto click1 = system.feed_point(p1);
  ASSERT_TRUE(click1) << click1.error();
  EXPECT_FALSE(*click1);
  {
    const auto controls = system.preview_control_polyline(cursor);
    ASSERT_EQ(controls.size(), 3u);
    EXPECT_GE(system.preview_polyline(cursor).size(), 5u);
  }

  auto click2 = system.feed_point(p2);
  ASSERT_TRUE(click2) << click2.error();
  EXPECT_FALSE(*click2);

  auto click3 = system.feed_point(p3);
  ASSERT_TRUE(click3) << click3.error();
  EXPECT_FALSE(*click3) << "Bezier must not finish on the fourth left click";
  EXPECT_EQ(doc.entities().size(), 0u);

  auto click4 = system.feed_point(p4);
  ASSERT_TRUE(click4) << click4.error();
  EXPECT_FALSE(*click4);
  {
    const auto controls = system.preview_control_polyline(cursor);
    ASSERT_EQ(controls.size(), 6u);
    EXPECT_NEAR(controls.back().x, cursor.x, 1e-5f);
    EXPECT_EQ(system.preview_points(cursor).size(), 6u);
  }

  auto done = system.confirm();
  ASSERT_TRUE(done) << done.error();
  EXPECT_TRUE(*done);
  ASSERT_EQ(doc.entities().size(), 1u);
  EXPECT_EQ(doc.entities().begin()->second->kind(), EntityKind::Bezier);
}

TEST(CurveGeom, BezierControlPointsAndQuadratic) {
  FeatureModel model;
  const Vec3 p0{0.f, 0.f, 0.f};
  const Vec3 p1{0.f, 0.f, 1.f};
  const Vec3 p2{2.f, 0.f, 1.f};
  const Vec3 p3{2.f, 0.f, 0.f};
  const Feature& f = model.add_feature(FeatureKind::Bezier, {}, bezier_feature_params(p0, p1, p2, p3));
  const auto ctrls = bezier_control_points(model, f);
  ASSERT_EQ(ctrls.size(), 4u);
  EXPECT_NEAR(ctrls[0].x, 0.f, 1e-5f);
  EXPECT_NEAR(ctrls[3].x, 2.f, 1e-5f);

  const auto quad = sample_quadratic_bezier(p0, p1, p2, 16);
  ASSERT_GE(quad.size(), 5u);
  EXPECT_NEAR(quad.front().x, p0.x, 1e-5f);
  EXPECT_NEAR(quad.back().x, p2.x, 1e-5f);

  const auto high = sample_bezier({p0, p1, p2, p3, {3.f, 0.f, -1.f}});
  ASSERT_GE(high.size(), 16u);
  EXPECT_NEAR(high.front().x, p0.x, 1e-5f);
  EXPECT_NEAR(high.back().x, 3.f, 1e-5f);
}

TEST(CommandSystem, CreatePolylineConfirm) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);

  Document doc("sketch-poly");
  ASSERT_TRUE(system.dispatch(doc, "create_polyline", {}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({1.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({1.f, 0.f, 1.f}));
  EXPECT_EQ(doc.entities().size(), 0u);
  auto done = system.confirm();
  ASSERT_TRUE(done) << done.error();
  EXPECT_TRUE(*done);
  ASSERT_EQ(doc.entities().size(), 1u);
  EXPECT_EQ(doc.entities().begin()->second->kind(), EntityKind::Polyline);
}

TEST(FeatureModel, FilletIncreasesFaces) {
  FeatureModel model;
  const std::uint64_t profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  model.add_feature(FeatureKind::Extrude, {profile}, {{"depth", 1.0}});

  auto before = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(before) << before.error();

  const std::uint64_t root = model.output_feature()->id;
  model.add_feature(FeatureKind::Fillet, {root}, {{"radius", 0.1}, {"edge", 0.0}});
  auto after = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(after) << after.error();
  // 圆角边引入更多三角面。
  EXPECT_GT(after->indices.size(), before->indices.size());
}

TEST(FeatureModel, ChamferProducesMesh) {
  FeatureModel model;
  const std::uint64_t profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  model.add_feature(FeatureKind::Extrude, {profile}, {{"depth", 1.0}});

  const std::uint64_t root = model.output_feature()->id;
  model.add_feature(FeatureKind::Chamfer, {root}, {{"distance", 0.1}, {"edge", 0.0}});
  auto mesh = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_FALSE(mesh->indices.empty());
  EXPECT_TRUE(mesh->bounds.valid());
}

TEST(FeatureModel, BooleanUnionAddsGeometry) {
  // 单独盒子。
  FeatureModel box;
  const std::uint64_t b =
      box.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  box.add_feature(FeatureKind::Extrude, {b}, {{"depth", 1.0}});
  auto box_mesh = evaluate_feature_model(box, 0.05);
  ASSERT_TRUE(box_mesh) << box_mesh.error();

  // 盒子 ∪ 圆柱（同坐标系内）。
  FeatureModel combined;
  const std::uint64_t p1 =
      combined.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  const std::uint64_t e1 = combined.add_feature(FeatureKind::Extrude, {p1}, {{"depth", 1.0}}).id;
  const std::uint64_t p2 = combined.add_feature(FeatureKind::CircleProfile, {}, {{"radius", 0.5}}).id;
  const std::uint64_t e2 = combined.add_feature(FeatureKind::Extrude, {p2}, {{"depth", 2.0}}).id;
  combined.add_feature(FeatureKind::Boolean, {e1, e2}, {{"operation", 0.0}});
  auto union_mesh = evaluate_feature_model(combined, 0.05);
  ASSERT_TRUE(union_mesh) << union_mesh.error();
  EXPECT_GT(union_mesh->indices.size(), box_mesh->indices.size());
}

TEST(CommandSystem, AddFeatureUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("cmd");

  // 建一面墙。
  ASSERT_TRUE(system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 5.f}));
  ASSERT_EQ(doc.entities().size(), 1u);
  const std::uint64_t eid = doc.entities().begin()->first;
  EXPECT_EQ(doc.entity(eid)->model.features().size(), 2u);

  // 倒圆角。
  auto r = system.dispatch(doc, "fillet",
                           {{"entity_id", static_cast<std::int64_t>(eid)}, {"radius", 0.05},
                            {"edge", static_cast<std::int64_t>(0)}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(doc.entity(eid)->model.features().size(), 3u);

  ASSERT_TRUE(system.can_undo());
  system.undo();
  EXPECT_EQ(doc.entity(eid)->model.features().size(), 2u);
  ASSERT_TRUE(system.can_redo());
  system.redo();
  EXPECT_EQ(doc.entity(eid)->model.features().size(), 3u);
}

TEST(CommandSystem, BooleanUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("cmd");

  // 建两个盒子（同放置，布尔合并局部几何）。
  ASSERT_TRUE(system.dispatch(doc, "create_box", {}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.dispatch(doc, "create_box", {}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_EQ(doc.entities().size(), 2u);

  std::uint64_t a = 0;
  std::uint64_t b = 0;
  for (const auto& [id, unused] : doc.entities()) {
    (void)unused;
    (a == 0) ? a = id : b = id;
  }

  auto r = system.dispatch(doc, "boolean",
                           {{"a", static_cast<std::int64_t>(a)}, {"b", static_cast<std::int64_t>(b)},
                            {"operation", static_cast<std::int64_t>(0)}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(doc.entities().size(), 1u);

  ASSERT_TRUE(system.can_undo());
  system.undo();
  EXPECT_EQ(doc.entities().size(), 2u);
  ASSERT_TRUE(system.can_redo());
  system.redo();
  EXPECT_EQ(doc.entities().size(), 1u);
}

TEST(CommandSystem, DeleteEntityUndoRedo) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("cmd");

  ASSERT_TRUE(system.dispatch(doc, "create_box", {}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_EQ(doc.entities().size(), 1u);
  const std::uint64_t eid = doc.entities().begin()->first;

  auto r = system.dispatch(doc, "delete_entity",
                           {{"entity_id", static_cast<std::int64_t>(eid)}});
  ASSERT_TRUE(r) << r.error();
  EXPECT_EQ(doc.entities().size(), 0u);
  EXPECT_TRUE(doc.meshes().empty());
  EXPECT_EQ(doc.selected_entity(), nullptr);

  ASSERT_TRUE(system.can_undo());
  system.undo();
  ASSERT_EQ(doc.entities().size(), 1u);
  EXPECT_NE(doc.entity(eid), nullptr);

  ASSERT_TRUE(system.can_redo());
  system.redo();
  EXPECT_EQ(doc.entities().size(), 0u);
  EXPECT_EQ(doc.entity(eid), nullptr);
}

TEST(Bim, HostPlacementAlignAndValidity) {
  WallEntity wall({0.f, 0.f, 0.f}, {0.f, 0.f, 5.f}, 0.2, 3.0);
  WindowEntity window({0.f, 0.9f, 2.5f}, 1.2, 1.2, 0.08);
  const WallSize size = wall_size(wall);
  EXPECT_NEAR(size.length, 5.0, 1e-6);
  EXPECT_NEAR(size.height, 3.0, 1e-6);

  HostPlacement placement = placement_from_world(wall, window, {0.f, 1.5f, 2.5f});
  align_placement(placement, size, opening_size(window));
  EXPECT_TRUE(placement_is_valid(placement, size, opening_size(window)));
  EXPECT_NEAR(placement.along, 0.5, 0.05);

  HostPlacement overflow{};
  overflow.along = 0.05;
  overflow.sill = 2.5;
  align_placement(overflow, size, opening_size(window));
  EXPECT_GE(overflow.along, 1.2 * 0.5 / 5.0 - 1e-6);
  EXPECT_LE(overflow.sill + 1.2, 3.0 + 1e-6);
  EXPECT_TRUE(placement_is_valid(overflow, size, opening_size(window)));
}

TEST(Bim, WallChangeNotifiesHostedWindow) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("host");

  ASSERT_TRUE(system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 5.f}));
  ASSERT_EQ(doc.entities().size(), 1u);
  const std::uint64_t wall_id = doc.entities().begin()->first;

  ASSERT_TRUE(system.dispatch(doc, "create_window", {}));
  ASSERT_TRUE(system.feed_point({0.f, 1.5f, 2.5f}, wall_id));
  ASSERT_EQ(doc.entities().size(), 2u);

  const Relation* rel = nullptr;
  std::uint64_t window_id = 0;
  for (const auto& [id, entity] : doc.entities()) {
    if (entity->kind() == EntityKind::Window) {
      window_id = id;
      rel = doc.bim().host_of(id);
    }
  }
  ASSERT_NE(rel, nullptr);
  EXPECT_EQ(rel->to, wall_id);
  EXPECT_EQ(rel->from, window_id);
  EXPECT_TRUE(rel->valid);

  Entity* wall = doc.entity(wall_id);
  ASSERT_NE(wall, nullptr);
  const std::uint64_t profile_id = wall->model.features().front().id;
  ASSERT_TRUE(system.dispatch(doc, "set_param",
                              {{"entity_id", static_cast<std::int64_t>(wall_id)},
                               {"feature_id", static_cast<std::int64_t>(profile_id)},
                               {"param_name", std::string("width")},
                               {"value", 0.4}}));

  const Relation* after_rel = doc.bim().host_of(window_id);
  ASSERT_NE(after_rel, nullptr);
  EXPECT_TRUE(after_rel->valid);
  EXPECT_NEAR(opening_size(*doc.entity(window_id)).thickness, 0.4, 1e-6);
}

TEST(Bim, RelationRoundTrip) {
  Document doc("rel");
  const std::uint64_t wall_id = add_wall_entity(doc, {0.f, 0.f, 0.f}, {0.f, 0.f, 5.f});
  auto window = std::make_unique<WindowEntity>(Vec3{0.f, 0.9f, 2.5f});
  MeshAsset mesh{};
  mesh.name = window->name;
  mesh.cpu = make_demo_cube();
  auto& stored = doc.add_mesh(std::move(mesh));
  window->mesh_asset_id = stored.id;
  SceneNode node{};
  node.name = window->name;
  node.mesh_asset_id = window->mesh_asset_id;
  SceneNode& stored_node = doc.scene().add_node(std::move(node));
  window->id = stored_node.id;
  const std::uint64_t window_id = window->id;
  doc.insert_entity(std::move(window));

  Relation rel{};
  rel.kind = RelationKind::HostedOn;
  rel.from = window_id;
  rel.to = wall_id;
  rel.placement.along = 0.42;
  rel.placement.sill = 0.9;
  rel.valid = true;
  const std::uint64_t rel_id = doc.bim().add(rel).id;

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();
  const Relation* loaded = restored->bim().host_of(window_id);
  ASSERT_NE(loaded, nullptr);
  EXPECT_EQ(loaded->id, rel_id);
  EXPECT_EQ(loaded->to, wall_id);
  EXPECT_NEAR(loaded->placement.along, 0.42, 1e-9);
  EXPECT_NEAR(loaded->placement.sill, 0.9, 1e-9);
  EXPECT_TRUE(loaded->valid);
}

