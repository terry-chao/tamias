#pragma once

#include "math/math.h"

#include <algorithm>
#include <cmath>

namespace tamias {

// Z-up turntable camera: yaw around Z, pitch from XY plane.
class TurntableCamera {
 public:
  void set_target(Vec3 target) { target_ = target; }
  void set_distance(float d) { distance_ = std::max(0.01f, d); }
  void set_yaw_pitch(float yaw, float pitch) {
    yaw_ = yaw;
    pitch_ = std::clamp(pitch, -1.5f, 1.5f);
  }
  void orbit(float dyaw, float dpitch) { set_yaw_pitch(yaw_ + dyaw, pitch_ + dpitch); }

  // Snap to axis-aligned views (Z-up): Front=-Y, Back=+Y, Right=+X, Left=-X.
  void look_front() { set_yaw_pitch(-1.570796327f, 0.f); }
  void look_back() { set_yaw_pitch(1.570796327f, 0.f); }
  void look_right() { set_yaw_pitch(0.f, 0.f); }
  void look_left() { set_yaw_pitch(3.141592654f, 0.f); }
  void look_top() { set_yaw_pitch(yaw_, 1.5f); }
  void look_bottom() { set_yaw_pitch(yaw_, -1.5f); }

  void pan(float dx, float dy) {
    const Vec3 eye = eye_position();
    const Vec3 forward = normalize(target_ - eye);
    const Vec3 right = normalize(cross(forward, {0.f, 0.f, 1.f}));
    const Vec3 up = cross(right, forward);
    target_ = target_ + right * dx + up * dy;
  }
  void dolly(float factor) { set_distance(distance_ * factor); }

  [[nodiscard]] Vec3 eye_position() const {
    const float cp = std::cos(pitch_);
    const float sp = std::sin(pitch_);
    const float cy = std::cos(yaw_);
    const float sy = std::sin(yaw_);
    return target_ + Vec3{distance_ * cp * cy, distance_ * cp * sy, distance_ * sp};
  }

  [[nodiscard]] Mat4 view_matrix() const {
    return look_at(eye_position(), target_, {0.f, 0.f, 1.f});
  }

  [[nodiscard]] Mat4 proj_matrix(float aspect) const {
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

 private:
  Vec3 target_{};
  float distance_ = 5.f;
  float yaw_ = 0.8f;
  float pitch_ = 0.5f;
  float fovy_ = 0.8f;
  float znear_ = 0.05f;
  float zfar_ = 500.f;
};

}  // namespace tamias
