#pragma once

#include "engine/math/math.h"

#include <algorithm>
#include <cmath>

namespace tamias {

inline constexpr float kHalfPi = 1.570796327f;

// Y-up turntable camera: yaw around Y, pitch from the XZ plane.
// Matches glTF / Blender: Front = +Z, Top = +Y, Right = +X.
class TurntableCamera {
 public:
  void set_target(Vec3 target) { target_ = target; }
  void set_distance(float d) { distance_ = std::max(0.01f, d); }
  void set_yaw_pitch(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -kHalfPi, kHalfPi);
  }
  void orbit(float dyaw, float dpitch) { set_yaw_pitch(yaw_ + dyaw, pitch_ + dpitch); }

  // Snap to axis-aligned views (Y-up): Front=+Z, Back=-Z, Right=+X, Left=-X.
  void look_front() { set_yaw_pitch(0.f, 0.f); }
  void look_back() { set_yaw_pitch(3.141592654f, 0.f); }
  void look_right() { set_yaw_pitch(kHalfPi, 0.f); }
  void look_left() { set_yaw_pitch(-kHalfPi, 0.f); }
  void look_top() { set_yaw_pitch(yaw_, kHalfPi); }
  void look_bottom() { set_yaw_pitch(yaw_, -kHalfPi); }
  // 2D plan: look down -Y onto the ground. Screen X = world +X, screen up = world +Z
  // (drawing Y). World Y is height, toward the camera.
  void look_plan() { set_yaw_pitch(0.f, kHalfPi); }

  // Place the eye along `direction` (from target toward eye), e.g. cube corners.
  void look_toward(Vec3 direction) {
    const Vec3 d = normalize(direction);
    const float pitch = std::asin(std::clamp(d.y, -1.f, 1.f));
    const float yaw = std::atan2(d.x, d.z);
    set_yaw_pitch(yaw, pitch);
  }

  void pan(float dx, float dy) {
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    axes(right, up, forward);
    (void)forward;
    target_ = target_ + right * dx + up * dy;
  }
  void dolly(float factor) { set_distance(distance_ * factor); }

  // Screen-aligned basis: `forward` looks at the target, `right`/`up` match view X/Y.
  void axes(Vec3& right, Vec3& up, Vec3& forward) const {
    // True top/bottom: world Y is the look axis, so do not use it as `up`.
    // Plan basis keeps +X to the right and world +Z (drawing Y) screen-up.
    if (pitch_ > kHalfPi - 0.02f) {
      forward = {0.f, -1.f, 0.f};
      right = {1.f, 0.f, 0.f};
      up = {0.f, 0.f, 1.f};
      return;
    }
    if (pitch_ < -kHalfPi + 0.02f) {
      forward = {0.f, 1.f, 0.f};
      right = {1.f, 0.f, 0.f};
      up = {0.f, 0.f, -1.f};
      return;
    }
    forward = normalize(target_ - eye_position());
    Vec3 world_up{0.f, 1.f, 0.f};
    Vec3 r = cross(forward, world_up);
    if (length(r) < 1e-4f) {
      world_up = {0.f, 0.f, forward.y >= 0.f ? -1.f : 1.f};
      r = cross(forward, world_up);
    }
    right = normalize(r);
    up = cross(right, forward);
  }

  [[nodiscard]] Vec3 eye_position() const {
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    // yaw=0,pitch=0 → (0, 0, +distance): looking toward -Z (Front).
    return target_ + Vec3{distance_ * cp * sy, distance_ * sp, distance_ * cp * cy};
  }

  [[nodiscard]] Mat4 view_matrix() const {
    Vec3 right;
    Vec3 up;
    Vec3 forward;
    axes(right, up, forward);
    const Vec3 eye = eye_position();
    Mat4 r = Mat4::identity();
    r(0, 0) = right.x;
    r(0, 1) = right.y;
    r(0, 2) = right.z;
    r(0, 3) = -dot(right, eye);
    r(1, 0) = up.x;
    r(1, 1) = up.y;
    r(1, 2) = up.z;
    r(1, 3) = -dot(up, eye);
    r(2, 0) = -forward.x;
    r(2, 1) = -forward.y;
    r(2, 2) = -forward.z;
    r(2, 3) = dot(forward, eye);
    return r;
  }

  [[nodiscard]] Mat4 proj_matrix(float aspect) const {
    if (orthographic_) {
      const float half_h = std::max(distance_ * std::tan(fovy_ * 0.5f), 0.01f);
      const float half_w = half_h * std::max(aspect, 1e-4f);
      return ortho(-half_w, half_w, -half_h, half_h, znear_, zfar_);
    }
    return perspective(fovy_, aspect, znear_, zfar_);
  }

  void frame_aabb(const Aabb& box) {
    if (!box.valid()) {
      return;
    }
    target_ = box.center();
    const Vec3 e = box.extent();
    const float radius = length(e) * 0.5f;
    distance_ = std::max(radius * 2.5f, 0.5f);
    znear_ = std::max(distance_ * 0.001f, 0.01f);
    zfar_ = std::max(distance_ * 20.f, 100.f);
  }

  [[nodiscard]] float yaw() const { return yaw_; }
  [[nodiscard]] float pitch() const { return pitch_; }
  [[nodiscard]] float distance() const { return distance_; }
  [[nodiscard]] Vec3 target() const { return target_; }
  [[nodiscard]] float fovy() const { return fovy_; }
  [[nodiscard]] float znear() const { return znear_; }
  [[nodiscard]] float zfar() const { return zfar_; }

  void set_fovy(float fovy) { fovy_ = fovy; }
  void set_znear(float znear) { znear_ = std::max(0.001f, znear); }
  void set_zfar(float zfar) { zfar_ = std::max(znear_ + 0.001f, zfar); }
  void set_orthographic(bool enabled) { orthographic_ = enabled; }
  [[nodiscard]] bool orthographic() const { return orthographic_; }

 private:
  Vec3 target_{};
  float distance_ = 5.f;
  // Default: elevated front-right three-quarter (Front=+Z, Right=+X).
  float yaw_ = 0.785398163f;  // +45°
  float pitch_ = 0.35f;       // ~20°
  float fovy_ = 0.8f;
  float znear_ = 0.05f;
  float zfar_ = 500.f;
  bool orthographic_ = false;
};

}  // namespace tamias
