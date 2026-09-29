#include "engine/interaction/drag.h"

namespace tamias {

// 唯一的默认分发实现。写成 out-of-line 是为了让 drag.h 保持接口表的可读性，
// 同时也给以后需要非内联实现的 Drag 成员留个落点。
//
// 注意：子类重写 on_pointer() 就完全绕过下面这批命名钩子——和 Qt 里重写
// QWidget::event() 之后 mousePressEvent() 不再被调用是同一个语义。
InteractionResult Drag::on_pointer(const PointerEvent& event) {
  switch (event.phase) {
    case PointerPhase::Down:
      switch (event.button) {
        case ButtonId::Primary:
          return on_left_button_down(event);
        case ButtonId::Secondary:
          return on_right_button_down(event);
        case ButtonId::Middle:
          return on_middle_button_down(event);
        case ButtonId::None:
        case ButtonId::Aux1:
        case ButtonId::Aux2:
          // 非标准按钮不猜：需要的话重写 on_pointer()。
          return {};
      }
      return {};
    case PointerPhase::Move:
      return on_mouse_move(event);
    case PointerPhase::Up:
      switch (event.button) {
        case ButtonId::Primary:
          return on_left_button_up(event);
        case ButtonId::Secondary:
          return on_right_button_up(event);
        case ButtonId::Middle:
          return on_middle_button_up(event);
        case ButtonId::None:
        case ButtonId::Aux1:
        case ButtonId::Aux2:
          return {};
      }
      return {};
    case PointerPhase::Cancel:
      // 设备丢失：按"松开按键"处理更符合直觉，所以默认给 on_pointer_cancel。
      return on_pointer_cancel(event);
  }
  return {};
}

}  // namespace tamias
