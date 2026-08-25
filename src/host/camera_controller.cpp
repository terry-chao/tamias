#include "host/camera_controller.h"

namespace tamias {

void CameraController::orbit(float dx, float dy) {
  camera_.orbit(dx * kOrbitScale, dy * kOrbitScale);
}

void CameraController::pan(float dx, float dy) {
  const float scale = camera_.distance() * kPanScale;
  camera_.pan(dx * scale, dy * scale);
}

void CameraController::dolly(float factor) {
  camera_.dolly(factor);
}

void CameraController::dolly_to_focus(float factor, const Vec3& focus) {
  const Vec3 old_target = camera_.target();
  camera_.dolly(factor);
  camera_.set_target(focus + (old_target - focus) * factor);
}

void CameraController::frame_aabb(const Aabb& bounds) {
  camera_.frame_aabb(bounds);
}

}  // namespace tamias
