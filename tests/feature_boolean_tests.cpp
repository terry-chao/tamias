#include "engine/modeling/feature.h"
#include "engine/modeling/occt_feature.h"

#include <gtest/gtest.h>

namespace tamias {
namespace {

FeatureModel extruded_box() {
  FeatureModel model;
  const std::uint64_t p =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  model.add_feature(FeatureKind::Extrude, {p}, {{"depth", 1.0}});
  return model;
}

FeatureModel box_and_cylinder(BooleanOp op) {
  FeatureModel model;
  const std::uint64_t p1 =
      model.add_feature(FeatureKind::RectProfile, {}, {{"width", 1.0}, {"height", 1.0}}).id;
  const std::uint64_t e1 = model.add_feature(FeatureKind::Extrude, {p1}, {{"depth", 1.0}}).id;
  const std::uint64_t p2 =
      model.add_feature(FeatureKind::CircleProfile, {}, {{"radius", 0.5}}).id;
  const std::uint64_t e2 = model.add_feature(FeatureKind::Extrude, {p2}, {{"depth", 2.0}}).id;
  model.add_feature(FeatureKind::Boolean, {e1, e2},
                    {{"operation", static_cast<double>(static_cast<std::uint8_t>(op))}});
  return model;
}

TEST(FeatureModel, BooleanCutKeepsMeshSmallerThanFuse) {
  auto box = evaluate_feature_model(extruded_box(), 0.05);
  ASSERT_TRUE(box) << box.error();

  auto fuse = evaluate_feature_model(box_and_cylinder(BooleanOp::Fuse), 0.05);
  ASSERT_TRUE(fuse) << fuse.error();
  auto cut = evaluate_feature_model(box_and_cylinder(BooleanOp::Cut), 0.05);
  ASSERT_TRUE(cut) << cut.error();
  EXPECT_FALSE(cut->indices.empty());
  EXPECT_TRUE(cut->bounds.valid());
  EXPECT_GT(fuse->indices.size(), box->indices.size());
  EXPECT_LT(cut->indices.size(), fuse->indices.size());
}

TEST(FeatureModel, BooleanCommonStaysInsideBox) {
  auto box = evaluate_feature_model(extruded_box(), 0.05);
  ASSERT_TRUE(box) << box.error();
  auto common = evaluate_feature_model(box_and_cylinder(BooleanOp::Common), 0.05);
  ASSERT_TRUE(common) << common.error();
  EXPECT_FALSE(common->indices.empty());
  EXPECT_TRUE(common->bounds.valid());
  EXPECT_LE(common->bounds.max.x, box->bounds.max.x + 1e-3f);
  EXPECT_GE(common->bounds.min.x, box->bounds.min.x - 1e-3f);
}

TEST(FeatureModel, CylinderRespectsTamiasAxis) {
  FeatureModel model;
  model.add_feature(FeatureKind::Cylinder, {},
                    {{"radius", 0.05},
                     {"height", 0.2},
                     {"cx", 1.0},
                     {"cy", 2.0},
                     {"cz", 3.0},
                     {"ax", 0.0},
                     {"ay", 0.0},
                     {"az", 1.0}});

  auto mesh = evaluate_feature_model(model, 0.02);
  ASSERT_TRUE(mesh) << mesh.error();
  const Vec3 extent = mesh->bounds.extent();
  EXPECT_NEAR(extent.x, 0.1f, 1e-3f);
  EXPECT_NEAR(extent.y, 0.1f, 1e-3f);
  EXPECT_NEAR(extent.z, 0.2f, 1e-3f);
  EXPECT_NEAR(mesh->bounds.center().x, 1.0f, 1e-3f);
  EXPECT_NEAR(mesh->bounds.center().y, 2.0f, 1e-3f);
  EXPECT_NEAR(mesh->bounds.center().z, 3.0f, 1e-3f);
}

}  // namespace
}  // namespace tamias
