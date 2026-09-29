#include "command/create/component/beam_drag.h"

#include <span>

namespace tamias {

InteractionResult BeamDrag::on_left_button_down(const PointerEvent& event) {
  if (context_ == nullptr) {
    return InteractionResult::cancelled();
  }
  const Vec3 point = context_->cursor_world(event.pos, plane_y_);
  if (!has_start_) {
    start_ = point;
    has_start_ = true;
    return InteractionResult::consumed();  // 等第二点
  }
  end_ = point;
  return InteractionResult::finished();  // 两点齐了 → 管理器结束，调用方提交
}

InteractionResult BeamDrag::on_mouse_move(const PointerEvent& event) {
  if (context_ == nullptr) {
    return InteractionResult::consumed();
  }
  const Vec3 cursor = context_->cursor_world(event.pos, plane_y_);
  if (has_start_) {
    // 第一点已落：橡皮筋。
    preview_line_.clear();
    preview_line_.push_back(start_);
    preview_line_.push_back(cursor);
    context_->set_drag_polyline(preview_line_);
  } else {
    // 还没落点：只画"光标落在工作面的哪儿"那个标记。
    context_->set_drag_polyline({});
    context_->set_drag_points(std::span<const Vec3>(&cursor, 1));
  }
  context_->request_redraw();
  return InteractionResult::consumed();
}

}  // namespace tamias
