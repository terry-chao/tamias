#pragma once

#include "engine/interaction/drag.h"

#include <string_view>
#include <vector>

namespace tamias {

// 梁的两点采集：左键点第一点 → 移动出橡皮筋 → 左键点第二点提交。
// 右键 / Esc 取消。
//
// 只采集，不建实体：提交由调用方（CreateBeamCommand）在 doIt() 返回后做，
// 这样拖拽期间文档一个字节都不动。
class BeamDrag final : public Drag {
 public:
  // work_plane_y：这一根梁落在哪个标高。由调用方算好（本层顶），drag 只负责用。
  explicit BeamDrag(float work_plane_y) : plane_y_(work_plane_y) {}

  [[nodiscard]] std::string_view name() const override { return "beam"; }

  [[nodiscard]] bool has_start() const { return has_start_; }
  [[nodiscard]] Vec3 start() const { return start_; }
  [[nodiscard]] Vec3 end() const { return end_; }

 protected:
  void on_start(DragContext& context) override { context_ = &context; }

  InteractionResult on_left_button_down(const PointerEvent& event) override;
  InteractionResult on_mouse_move(const PointerEvent& event) override;

  InteractionResult on_right_button_down(const PointerEvent&) override {
    return InteractionResult::cancelled();
  }
  // 设备丢失（Cancel 相位）也算放弃，否则 drag 会挂在那儿等。
  InteractionResult on_pointer_cancel(const PointerEvent&) override {
    return InteractionResult::cancelled();
  }
  InteractionResult on_key(const KeyEvent& event) override {
    if (event.down && event.code == KeyCode::Escape) {
      return InteractionResult::cancelled();
    }
    return InteractionResult::consumed();
  }

 private:
  DragContext* context_ = nullptr;
  float plane_y_ = 0.f;
  bool has_start_ = false;
  Vec3 start_{};
  Vec3 end_{};
  // 复用缓冲：每帧重建预览线不该反复分配。
  std::vector<Vec3> preview_line_{};
};

}  // namespace tamias
