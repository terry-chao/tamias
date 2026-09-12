#include "host/camera_controller.h"

#include "engine/document/picking.h"

#include <gtest/gtest.h>

#include <cmath>

namespace tamias {
namespace {

float dist(const Vec3& a, const Vec3& b) {
  const float dx = a.x - b.x;
  const float dy = a.y - b.y;
  const float dz = a.z - b.z;
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

TEST(CameraController, OrbitMovesEye) {
  CameraController cam;
  const Vec3 eye0 = cam.camera().eye_position();
  const float yaw0 = cam.camera().yaw();
  cam.orbit(100.f, 0.f);
  EXPECT_GT(std::abs(cam.camera().yaw() - yaw0), 0.5f);
  EXPECT_GT(dist(cam.camera().eye_position(), eye0), 0.1f);
}

TEST(CameraController, PanMovesTarget) {
  CameraController cam;
  const Vec3 target0 = cam.camera().target();
  cam.pan(80.f, 0.f);
  EXPECT_GT(dist(cam.camera().target(), target0), 0.01f);
}

TEST(CameraController, FrameAabbCentersAndPullsBack) {
  CameraController cam;
  Aabb box;
  box.expand({-2.f, 0.f, -2.f});
  box.expand({2.f, 4.f, 2.f});
  cam.frame_aabb(box);
  const Vec3 c = box.center();
  EXPECT_NEAR(cam.camera().target().x, c.x, 1e-4f);
  EXPECT_NEAR(cam.camera().target().y, c.y, 1e-4f);
  EXPECT_NEAR(cam.camera().target().z, c.z, 1e-4f);
  EXPECT_GT(cam.camera().distance(), 1.f);
}

TEST(CameraController, DollyToFocusPullsTargetTowardFocus) {
  CameraController cam;
  const Vec3 focus{1.f, 0.f, 0.f};
  const Vec3 target0 = cam.camera().target();
  const float d0 = dist(target0, focus);
  cam.dolly_to_focus(0.5f, focus);
  const float d1 = dist(cam.camera().target(), focus);
  EXPECT_LT(d1, d0);
}

// 视口右边是工具列，相机只渲染左边的三维区域。取点射线必须和渲染用同一个区域
// 尺寸：用整块视口的宽（含工具列）算，光标下的点会横向偏离——画墙时"墙不在
// 鼠标下起笔"就是这个。
TEST(CameraRay, UsesRenderedAreaSoMousePointRoundTrips) {
  TurntableCamera camera;
  camera.set_target({0.f, 0.f, 0.f});
  camera.set_distance(12.f);
  camera.set_yaw_pitch(0.6f, 0.5f);

  const float area_w = 800.f;
  const float area_h = 600.f;
  const float mouse_x = 620.f;
  const float mouse_y = 250.f;

  const Ray ray = camera_ray(camera, area_w / area_h, mouse_x, mouse_y, area_w, area_h);
  ASSERT_LT(ray.direction.y, 0.f);
  const float t = -ray.origin.y / ray.direction.y;
  const Vec3 hit = ray.origin + ray.direction * t;

  // 同一个世界点投回屏幕，应该回到鼠标位置。
  const Mat4 view_proj = camera.proj_matrix(area_w / area_h) * camera.view_matrix();
  float sx = 0.f;
  float sy = 0.f;
  ASSERT_TRUE(project_world_to_screen(view_proj, hit, area_w, area_h, sx, sy));
  EXPECT_NEAR(sx, mouse_x, 0.5f);
  EXPECT_NEAR(sy, mouse_y, 0.5f);

  // 拿整块视口宽（右侧工具列也算进去）来算，就会落到别的世界点上。
  const float widget_w = 1130.f;
  const Ray wide = camera_ray(camera, widget_w / area_h, mouse_x, mouse_y, widget_w, area_h);
  const float t_wide = -wide.origin.y / wide.direction.y;
  const Vec3 wide_hit = wide.origin + wide.direction * t_wide;
  EXPECT_GT(dist(hit, wide_hit), 0.1f);
}

}  // namespace
}  // namespace tamias
