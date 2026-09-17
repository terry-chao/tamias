#include "engine/drawing/drawing_placement.h"

#include "engine/document/picking.h"
#include "engine/math/camera.h"

#include <gtest/gtest.h>

#include <cmath>

namespace tamias {
namespace {

// 单位四边形的四个角 → 世界。
Vec3 corner(const Mat4& m, float u, float v) { return m * Vec3{u, 0.f, v}; }

Aabb2 page(float width, float height) {
  Aabb2 box{};
  box.expand(0.f, 0.f);
  box.expand(width, height);
  return box;
}

}  // namespace

// 摆放约定：图纸原点落在 (offset_x, elevation, offset_z)，图纸 +X 沿世界 +X、
// 图纸"上"（+Y）朝世界 +Z（和翻模 / 平面视图同一套映射）；
// 图片第 0 行（图纸最上面）对应局部 v = 0。
TEST(DrawingPlacement, OriginAndOrientation) {
  DrawingPlacement placement;
  placement.scale = 0.1;
  placement.elevation = 3.0;
  const Mat4 m = drawing_plane_transform(page(10.f, 5.f), placement);

  // 图纸 (0,0)（页左下角）= 局部 (u,v) = (0,1)。
  const Vec3 origin = corner(m, 0.f, 1.f);
  EXPECT_NEAR(origin.x, 0.0f, 1e-5f);
  EXPECT_NEAR(origin.y, 3.0f, 1e-5f);
  EXPECT_NEAR(origin.z, 0.0f, 1e-5f);

  // 图纸 (10,5)（页右上角）= 局部 (1,0)：x 向右 +0.1*10，z 朝 +Z 走 +0.1*5。
  const Vec3 top_right = corner(m, 1.f, 0.f);
  EXPECT_NEAR(top_right.x, 1.0f, 1e-5f);
  EXPECT_NEAR(top_right.z, 0.5f, 1e-5f);
}

TEST(DrawingPlacement, OffsetAndRotation) {
  DrawingPlacement placement;
  placement.rotation_deg = 90.0;
  placement.offset_x = 2.0;
  placement.offset_z = -1.0;
  placement.elevation = 0.5;
  const Mat4 m = drawing_plane_transform(page(10.f, 10.f), placement);

  // 图纸原点仍在落点上。
  const Vec3 origin = corner(m, 0.f, 1.f);
  EXPECT_NEAR(origin.x, 2.0f, 1e-4f);
  EXPECT_NEAR(origin.y, 0.5f, 1e-4f);
  EXPECT_NEAR(origin.z, -1.0f, 1e-4f);

  // 转 90° 后图纸 +X 指向世界 +Z（俯视面上逆时针：右边转到屏幕上方）。
  const Vec3 x_end = corner(m, 1.f, 1.f);  // 图纸 (10, 0)
  EXPECT_NEAR(x_end.x, 2.0f, 1e-4f);
  EXPECT_NEAR(x_end.z, 9.0f, 1e-4f);
}

// 图纸写了单位就用它，不去猜；页中心压到模型范围中心上。
TEST(DrawingPlacement, DefaultUsesDeclaredUnitsAndCentres) {
  Aabb2 footprint{};
  footprint.expand(-5.f, -5.f);
  footprint.expand(5.f, 5.f);

  const DrawingPlacement placement =
      default_drawing_placement(page(1000.f, 500.f), footprint, 3.2, 0.001);
  EXPECT_DOUBLE_EQ(placement.scale, 0.001);
  EXPECT_DOUBLE_EQ(placement.elevation, 3.2);

  const Aabb2 world = drawing_plane_footprint(page(1000.f, 500.f), placement);
  EXPECT_NEAR(0.5 * (world.min_x + world.max_x), 0.0f, 1e-4f);
  EXPECT_NEAR(0.5 * (world.min_y + world.max_y), 0.0f, 1e-4f);
  EXPECT_NEAR(world.width(), 1.0f, 1e-4f);   // 1000 mm = 1 m
  EXPECT_NEAR(world.height(), 0.5f, 1e-4f);  // 500 mm = 0.5 m
}

// 图纸没写单位（$INSUNITS = 0）时按模型范围等比塞进去，并居中——
// 直接按 1:1 摆一张毫米图纸会大得看不见。
TEST(DrawingPlacement, DefaultFitsModelWhenUnitsMissing) {
  Aabb2 footprint{};
  footprint.expand(-5.f, -2.f);
  footprint.expand(5.f, 2.f);  // 10 m × 4 m

  const DrawingPlacement placement =
      default_drawing_placement(page(1000.f, 500.f), footprint, 0.0, 0.0);
  EXPECT_DOUBLE_EQ(placement.scale, 0.008);  // min(10/1000, 4/500)

  const Aabb2 world = drawing_plane_footprint(page(1000.f, 500.f), placement);
  EXPECT_NEAR(0.5 * (world.min_x + world.max_x), 0.0f, 1e-4f);
  EXPECT_NEAR(0.5 * (world.min_y + world.max_y), 0.0f, 1e-4f);
  EXPECT_LE(world.width(), 10.001f);
  EXPECT_LE(world.height(), 4.001f);
}

// 底图不能镜像：图纸"上"必须落在平面视图的屏幕上方。
// 这条把两处约定钉在一起——摆放矩阵（图纸 +Y → 世界 +Z）和相机
//（平面视图里世界 +Z 是屏幕上方，见 TurntableCamera::look_plan / 拾取测试）。
TEST(DrawingPlacement, PlanViewShowsTheDrawingUpright) {
  const Aabb2 page = [] {
    Aabb2 box{};
    box.expand(0.f, 0.f);
    box.expand(10.f, 10.f);
    return box;
  }();
  DrawingPlacement placement;
  placement.scale = 1.0;
  const Mat4 m = drawing_plane_transform(page, placement);
  // 图纸 (0,0)（页左下角）与 (0,10)（页左上角）落在世界里的位置。
  const Vec3 bottom = corner(m, 0.f, 1.f);
  const Vec3 top = corner(m, 0.f, 0.f);

  TurntableCamera camera;
  camera.set_target({5.f, 0.f, 5.f});
  camera.set_distance(30.f);
  camera.look_plan();
  camera.set_orthographic(true);
  const Mat4 vp = camera.proj_matrix(1.f) * camera.view_matrix();
  float bx = 0.f;
  float by = 0.f;
  float tx = 0.f;
  float ty = 0.f;
  ASSERT_TRUE(project_world_to_screen(vp, bottom, 400.f, 400.f, bx, by));
  ASSERT_TRUE(project_world_to_screen(vp, top, 400.f, 400.f, tx, ty));
  EXPECT_NEAR(tx, bx, 1.f);  // 同一条竖线
  EXPECT_LT(ty, by);         // 图纸上方在屏幕上方（屏幕 y 向下）
  // 正交视口半高 = distance * tan(fovy/2)，10 m 的图纸高度占多少像素照这个算。
  const float half_height = 30.f * std::tan(camera.fovy() * 0.5f);
  const float expected_px = (10.f / half_height) * 200.f;
  EXPECT_NEAR(by - ty, expected_px, 2.f);
}

}  // namespace tamias
