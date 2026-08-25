#pragma once

#include "engine/math/camera.h"

namespace tamias {

// 轨道相机 + 手势参数，桌面 / wasm 共用。
// 壳只负责「哪个按钮进哪个手势」，数学和手感只存在这里一份。
class CameraController {
 public:
  [[nodiscard]] TurntableCamera& camera() { return camera_; }
  [[nodiscard]] const TurntableCamera& camera() const { return camera_; }

  // 增量手势（dx/dy 为屏幕像素增量，符号与桌面视口一致）。
  void orbit(float dx, float dy);
  void pan(float dx, float dy);
  void dolly(float factor);
  // 滚轮聚焦缩放：先 dolly，再把目标点沿视线收拢，使 focus 在屏幕上保持不动。
  void dolly_to_focus(float factor, const Vec3& focus);
  void frame_aabb(const Aabb& bounds);

 private:
  static constexpr float kOrbitScale = 0.01f;   // 桌面视口原值
  static constexpr float kPanScale = 0.002f;    // 桌面视口原值（按 distance 缩放）

  TurntableCamera camera_;
};

}  // namespace tamias
