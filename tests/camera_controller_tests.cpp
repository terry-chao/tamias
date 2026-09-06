#include "host/camera_controller.h"

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

}  // namespace
}  // namespace tamias
