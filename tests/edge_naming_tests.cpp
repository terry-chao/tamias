#include "command/command_system.h"
#include "engine/document/document.h"
#include "engine/document/document_io.h"
#include "engine/graphics/mesh.h"
#include "engine/modeling/edge_fingerprint.h"
#include "engine/modeling/feature.h"
#include "engine/modeling/occt_feature.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>

namespace tamias {
namespace {

FeatureModel extruded_box(double width, double height, double depth) {
  FeatureModel model;
  const std::uint64_t profile =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", width}, {"height", height}}).id;
  model.add_feature(FeatureKind::Extrude, {profile}, {{"depth", depth}});
  return model;
}

std::uint64_t add_fillet(FeatureModel& model, int edge_index, double radius) {
  const std::uint64_t input = model.output_feature()->id;
  const std::uint64_t id =
      model
          .add_feature(FeatureKind::Fillet, {input},
                       {{"radius", radius}, {"edge", static_cast<double>(edge_index)}})
          .id;
  refresh_edge_fingerprint(model, id);
  return id;
}

// 八个包围盒角里，还有没有顶点正好落在角上（= 那个角没被圆角 / 倒角吃掉）。
// 圆角倒对了边，缺的两个角就一直缺；倒错边，缺的角就换了一对。
std::uint8_t sharp_corner_mask(const MeshCpu& mesh) {
  const Aabb& box = mesh.bounds;
  constexpr float kEps = 1e-3f;
  std::uint8_t mask = 0;
  for (int i = 0; i < 8; ++i) {
    const Vec3 corner{(i & 1) != 0 ? box.max.x : box.min.x,
                      (i & 2) != 0 ? box.max.y : box.min.y,
                      (i & 4) != 0 ? box.max.z : box.min.z};
    for (const Vertex& v : mesh.vertices) {
      if (std::fabs(v.position.x - corner.x) <= kEps &&
          std::fabs(v.position.y - corner.y) <= kEps &&
          std::fabs(v.position.z - corner.z) <= kEps) {
        mask = static_cast<std::uint8_t>(mask | (1u << i));
        break;
      }
    }
  }
  return mask;
}

}  // namespace

TEST(EdgeNaming, FilletCapturesFingerprint) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t id = add_fillet(model, 0, 0.1);
  const Feature* fillet = model.find(id);
  ASSERT_NE(fillet, nullptr);
  EXPECT_TRUE(has_edge_fingerprint(fillet->params));
  EXPECT_TRUE(fillet->params.find("edge_len") != fillet->params.end());
  EXPECT_TRUE(fillet->params.find("edge_dir_x") != fillet->params.end());
}

TEST(EdgeNaming, ChamferCapturesFingerprint) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t input = model.output_feature()->id;
  const std::uint64_t id =
      model
          .add_feature(FeatureKind::Chamfer, {input}, {{"distance", 0.1}, {"edge", 0.0}})
          .id;
  refresh_edge_fingerprint(model, id);
  const Feature* chamfer = model.find(id);
  ASSERT_NE(chamfer, nullptr);
  EXPECT_TRUE(has_edge_fingerprint(chamfer->params));
  auto mesh = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh) << mesh.error();
  EXPECT_NE(sharp_corner_mask(*mesh), 0xFF);
}

TEST(EdgeNaming, FingerprintFollowsEdgeThroughUpstreamResize) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  add_fillet(model, 0, 0.1);

  auto before = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(before) << before.error();
  const std::uint8_t rounded_corners = sharp_corner_mask(*before);
  ASSERT_NE(rounded_corners, 0xFF);  // 先确认真的倒掉了一个角

  // 改上游参数：整个盒子重算，边的遍历顺序可能变。
  const std::uint64_t profile = model.features().front().id;
  model.set_param(profile, "width", 2.4);

  auto after = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(after) << after.error();
  EXPECT_EQ(sharp_corner_mask(*after), rounded_corners);
}

TEST(EdgeNaming, FingerprintOverridesStaleIndex) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t fillet_id = add_fillet(model, 0, 0.1);

  auto reference = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(reference) << reference.error();
  const std::uint8_t expected = sharp_corner_mask(*reference);
  ASSERT_NE(expected, 0xFF);

  // 先找一个「纯索引」会倒到别的棱上的编号，证明这个测试真的在验指纹。
  int other = -1;
  for (int i = 0; i < 16 && other < 0; ++i) {
    FeatureModel probe = extruded_box(1.0, 1.0, 1.0);
    const std::uint64_t input = probe.output_feature()->id;
    probe.add_feature(FeatureKind::Fillet, {input},
                      {{"radius", 0.1}, {"edge", static_cast<double>(i)}});
    auto mesh = evaluate_feature_model(probe, 0.05);
    if (mesh && sharp_corner_mask(*mesh) != expected) {
      other = i;
    }
  }
  ASSERT_GE(other, 0) << "没有找到会改变圆角位置的边编号，测试无法验证";

  // 索引被改错（模拟上游改动后 OCCT 重新编号）：指纹应该把它拉回原来那条边。
  Feature* fillet = model.find(fillet_id);
  ASSERT_NE(fillet, nullptr);
  fillet->params["edge"] = static_cast<double>(other);

  auto after = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(after) << after.error();
  EXPECT_EQ(sharp_corner_mask(*after), expected);
}

TEST(EdgeNaming, UnmatchableFingerprintReportsError) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t fillet_id = add_fillet(model, 0, 0.1);
  Feature* fillet = model.find(fillet_id);
  ASSERT_NE(fillet, nullptr);

  // 把指纹挪到模型外面：任何一条边都对不上 → 必须报错，而不是随便倒一条。
  fillet->params["edge_mid_x"] = -5.0;
  fillet->params["edge_mid_y"] = -5.0;
  fillet->params["edge_mid_z"] = -5.0;

  auto mesh = evaluate_feature_model(model, 0.05);
  ASSERT_FALSE(mesh);
  EXPECT_NE(mesh.error().find("edge"), std::string::npos);
}

TEST(EdgeNaming, LegacyIndexWithoutFingerprintStillWorks) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t input = model.output_feature()->id;
  model.add_feature(FeatureKind::Fillet, {input}, {{"radius", 0.1}, {"edge", 0.0}});
  auto mesh = evaluate_feature_model(model, 0.05);
  ASSERT_TRUE(mesh) << mesh.error();  // 旧文件 / 脚本只有索引 → 保持旧行为，不报错
}

TEST(EdgeNaming, RefreshDropsFingerprintWhenIndexOutOfRange) {
  FeatureModel model = extruded_box(1.0, 1.0, 1.0);
  const std::uint64_t fillet_id = add_fillet(model, 0, 0.1);
  Feature* fillet = model.find(fillet_id);
  ASSERT_NE(fillet, nullptr);
  ASSERT_TRUE(has_edge_fingerprint(fillet->params));

  fillet->params["edge"] = 9999.0;
  refresh_edge_fingerprint(model, fillet_id);
  EXPECT_FALSE(has_edge_fingerprint(model.find(fillet_id)->params));
}

TEST(EdgeNaming, FilletCommandStoresFingerprint) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("edge-naming");

  ASSERT_TRUE(system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 5.f}));
  ASSERT_EQ(doc.entities().size(), 1u);
  const std::uint64_t entity_id = doc.entities().begin()->first;

  auto r = system.dispatch(doc, "fillet",
                           {{"entity_id", static_cast<std::int64_t>(entity_id)},
                            {"radius", 0.05},
                            {"edge", static_cast<std::int64_t>(0)}});
  ASSERT_TRUE(r) << r.error();

  const Feature* fillet = &doc.entity(entity_id)->model.features().back();
  ASSERT_NE(fillet, nullptr);
  EXPECT_EQ(fillet->kind, FeatureKind::Fillet);
  EXPECT_TRUE(has_edge_fingerprint(fillet->params));
}

TEST(EdgeNaming, ChangingEdgeIndexRefreshesFingerprint) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("edge-naming-refresh");

  ASSERT_TRUE(system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 5.f}));
  const std::uint64_t entity_id = doc.entities().begin()->first;

  ASSERT_TRUE(system.dispatch(doc, "fillet",
                              {{"entity_id", static_cast<std::int64_t>(entity_id)},
                               {"radius", 0.05},
                               {"edge", static_cast<std::int64_t>(0)}}));
  Entity* entity = doc.entity(entity_id);
  ASSERT_NE(entity, nullptr);
  const std::uint64_t fillet_id = entity->model.features().back().id;

  const std::uint64_t input_id = entity->model.find(fillet_id)->inputs[0];
  ASSERT_TRUE(system.dispatch(doc, "set_param",
                              {{"entity_id", static_cast<std::int64_t>(entity_id)},
                               {"feature_id", static_cast<std::int64_t>(fillet_id)},
                               {"param_name", std::string("edge")},
                               {"value", 3.0}}));

  const Feature* updated = doc.entity(entity_id)->model.find(fillet_id);
  ASSERT_NE(updated, nullptr);
  ASSERT_TRUE(has_edge_fingerprint(updated->params));
  auto expected = capture_edge_fingerprint(doc.entity(entity_id)->model, input_id, 3);
  ASSERT_TRUE(expected) << expected.error();
  EXPECT_NEAR(updated->params.at("edge_mid_x"), expected->mid.x, 1e-6);
  EXPECT_NEAR(updated->params.at("edge_mid_y"), expected->mid.y, 1e-6);
  EXPECT_NEAR(updated->params.at("edge_mid_z"), expected->mid.z, 1e-6);
}

TEST(EdgeNaming, FingerprintSurvivesDocumentRoundTrip) {
  CommandRegistry registry;
  register_commands(registry);
  CommandSystem system(registry);
  Document doc("edge-naming-io");

  ASSERT_TRUE(system.dispatch(doc, "create_wall", {{"thickness", 0.2}, {"height", 3.0}}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 0.f}));
  ASSERT_TRUE(system.feed_point({0.f, 0.f, 5.f}));
  const std::uint64_t entity_id = doc.entities().begin()->first;
  ASSERT_TRUE(system.dispatch(doc, "fillet",
                              {{"entity_id", static_cast<std::int64_t>(entity_id)},
                               {"radius", 0.05},
                               {"edge", static_cast<std::int64_t>(0)}}));

  auto bytes = serialize_document(doc);
  ASSERT_TRUE(bytes) << bytes.error();
  auto restored = deserialize_document(*bytes);
  ASSERT_TRUE(restored) << restored.error();

  const Entity* entity = restored->entity(entity_id);
  ASSERT_NE(entity, nullptr);
  const Feature* fillet = &entity->model.features().back();
  ASSERT_EQ(fillet->kind, FeatureKind::Fillet);
  ASSERT_TRUE(has_edge_fingerprint(fillet->params));

  // 打开后模型是重新求值的：指纹还在，就该解析得出来。
  auto mesh = evaluate_feature_model(entity->model, 0.05);
  ASSERT_TRUE(mesh) << mesh.error();
}

}  // namespace tamias
