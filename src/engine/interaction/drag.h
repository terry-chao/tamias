#pragma once

#include "engine/base/result.h"
#include "engine/interaction/input_event.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace tamias {

// 谁拥有这次 drag（壳里的一个视口）。0 = 无主。
using DragOwnerId = std::uint64_t;

enum class CursorShape : std::uint8_t {
  Arrow,
  SizeAll,
  Cross,
  Hand,
  ResizeNS,
  ResizeEW,
  Panning,
};

// drag 结束的原因。管理器保证每个 drag 恰好收到一次 on_end。
enum class DragEnd : std::uint8_t {
  Committed,  // 正常完成
  Cancelled,  // 放弃：Esc / 被新交互抢占 / 宿主显式取消
  Clicked,    // 按下后没越过拖拽阈值就抬起：这是一次点击，不是拖拽
  OwnerGone,  // 宿主（视口/文档）销毁
  Timeout,    // 看门狗超时
};

[[nodiscard]] constexpr std::string_view to_string(DragEnd end) {
  switch (end) {
    case DragEnd::Committed:
      return "committed";
    case DragEnd::Cancelled:
      return "cancelled";
    case DragEnd::Clicked:
      return "clicked";
    case DragEnd::OwnerGone:
      return "owner_gone";
    case DragEnd::Timeout:
      return "timeout";
  }
  return "unknown";
}

// 每个事件处理完返回的状态。决定管理器下一步做什么。
enum class EventStatus : std::uint8_t {
  Ignored,    // 不消费：落到宿主默认逻辑（悬停高亮、右键菜单…）
  Consumed,   // 消费，drag 继续
  Finished,   // 消费，正常结束
  Cancelled,  // 消费，放弃
};

struct InteractionResult {
  EventStatus status = EventStatus::Ignored;

  static constexpr InteractionResult ignored() { return {}; }
  static constexpr InteractionResult consumed() { return {EventStatus::Consumed}; }
  static constexpr InteractionResult finished() { return {EventStatus::Finished}; }
  static constexpr InteractionResult cancelled() { return {EventStatus::Cancelled}; }
};

// 屏幕空间的矩形预览（框选那种虚线框走这里，不走三维预览线）。
struct ScreenRect {
  Vec2 min{};
  Vec2 max{};
  bool crossing = false;  // 从右往左拖 = 交叉框
};

// 宿主（视口）给 drag 的能力。由 app 层实现；交互层不认识 Qt / 窗口 / 渲染。
//
// "拖拽期间要画什么"是 **drag 推给宿主**，不是宿主动来问 drag：
// drag 在自己的 on_mouse_move 里把当前形状推一次，宿主负责塞进下一帧。
// 这样基类不需要一个多态的 preview()，也不需要每帧把预览拷一遍。
class DragContext {
 public:
  virtual ~DragContext() = default;
  [[nodiscard]] virtual DragOwnerId owner() const = 0;
  [[nodiscard]] virtual Vec2 viewport_size() const = 0;
  [[nodiscard]] virtual float device_pixel_ratio() const = 0;
  // 宿主是否还活着。视口析构 / 文档被换掉之后必须返回 false。
  [[nodiscard]] virtual bool alive() const = 0;
  virtual void request_redraw() = 0;

  // 屏幕点（控件局部逻辑像素）→ **指定标高平面**上的世界点。宿主负责投影、
  // 平面求交、网格捕捉这些它自己才知道的事；drag 因此不用认识相机和拾取。
  //
  // plane_y 由 drag 给：采集工具自己知道该落在哪个标高（梁在本层顶、板在板顶、
  // 多段线在地面）。**不能让宿主去猜**——命令不走 pending 之后，宿主手上没有
  // 工作面这个概念了。
  [[nodiscard]] virtual Vec3 cursor_world(Vec2 screen_pos, float plane_y) const = 0;

  // —— 预览出口（世界坐标，和 FrameSubmission::preview_* 一一对应）——
  // span 只在本次调用内有效；宿主该拷就拷。宿主可以复用缓冲，避免每帧分配。
  virtual void set_drag_polyline(std::span<const Vec3>) {}
  virtual void set_drag_control_polyline(std::span<const Vec3>) {}
  virtual void set_drag_points(std::span<const Vec3>) {}
  // 屏幕空间的框选矩形；nullopt = 没有框。虚线框由宿主决定怎么画（overlay 或渲染）。
  virtual void set_drag_screen_rect(std::optional<ScreenRect>) {}
  // 管理器在 drag 开始 / 结束时保证调一次，避免上一轮的橡皮筋留在屏幕上。
  virtual void clear_drag_preview() {}

  virtual void set_cursor(CursorShape) {}
  virtual void set_status_text(std::string_view) {}
};

// 你要继承的那个东西：重写鼠标 / 键盘方法即可。
//
// 两层入口，和 Qt 的 QWidget::event() / mousePressEvent() 是同一个关系：
//  · on_pointer()  底层分发点。需要自己处理全部指针事件（或非标准按钮）时重写它；
//                  重写它就绕过了下面那批命名钩子。
//  · on_left_button_down() / on_mouse_move() / … 命名钩子，日常写 drag 只重写这几个。
//
// 契约（写死在这里，新增 drag 必须遵守）：
//  1. 这些回调只通过 DragContext 推**预览**，绝不碰文档；
//  2. 提交文档只能在 on_end(Committed) 里做一次，并且必须走 Command（可撤销）；
//  3. on_end 幂等，且一定被调用恰好一次（含取消 / 超时 / 宿主销毁）；
//  4. 返回 Ignored 表示"这个事件我不管"，否则表示"我消费了"。
class Drag {
 public:
  virtual ~Drag() = default;
  [[nodiscard]] virtual std::string_view name() const = 0;

  // 阻塞运行：把 this **借**给管理器，直到 on_end 才返回 —— 也就是最初设想的
  // `SomeDragClass drag; drag.doIt(ctx);` 那个写法。
  //
  // 与 DragManager::start() 的区别是所有权：start() 接管 unique_ptr，doIt() 只借用，
  // 所以 this 可以是个栈对象，但必须活到这次调用返回为止。
  // 需要管理器上已装 EventSource（见 DragManager::set_event_source）。
  [[nodiscard]] Result<DragEnd> doIt(DragContext& context);

 protected:
  // 下面全是"管理器调用 / 子类重写"的回调，**故意不是 public**：
  // 外部直接调 on_pointer() 会绕过管理器的阈值判定、指针捕获、owner 过滤、
  // 看门狗，以及 InteractionResult → DragEnd 的翻译。唯一被允许的调用者是
  // DragManager（friend）；子类照常重写。
  //
  // 不用 private 是为了让子类还能"只插手一部分、其余照旧"：
  //     if (特殊事件) return ...;
  //     return Drag::on_pointer(event);
  friend class DragManager;

  // 被挂上时调用一次：可以在这里索要捕捉、压光标、准备预览缓冲。
  // 想在后续事件里推预览，就把 context 存下来（它的生命周期由管理器
  // 的借用 / 持有规则 + owner_destroyed 保证覆盖整个 drag）。
  virtual void on_start(DragContext&) {}

  // 底层分发点：默认按相位 / 按钮转到下面的命名钩子。
  virtual InteractionResult on_pointer(const PointerEvent&);

  // —— 命名钩子（默认什么都不做）——
  virtual InteractionResult on_left_button_down(const PointerEvent&) { return {}; }
  virtual InteractionResult on_middle_button_down(const PointerEvent&) { return {}; }
  virtual InteractionResult on_right_button_down(const PointerEvent&) { return {}; }
  virtual InteractionResult on_left_button_up(const PointerEvent&) { return {}; }
  virtual InteractionResult on_middle_button_up(const PointerEvent&) { return {}; }
  virtual InteractionResult on_right_button_up(const PointerEvent&) { return {}; }
  virtual InteractionResult on_mouse_move(const PointerEvent&) { return {}; }
  virtual InteractionResult on_pointer_cancel(const PointerEvent&) { return {}; }

  virtual InteractionResult on_key(const KeyEvent&) { return {}; }
  virtual InteractionResult on_wheel(const WheelEvent&) { return {}; }

  // 指针第一次越过拖拽阈值。只有 press 起手的 drag 会收到；
  // 命令激活式的 drag 和"点击"都不会触发它。
  virtual void on_threshold_crossed(const PointerEvent&) {}

  virtual void on_end(DragEnd) {}
};

// press 起手的仲裁器：决定"这一下按下归不归我"。无状态、可复用。
// 注册顺序 = 优先级，先注册的先有机会接管。
class DragHandler {
 public:
  virtual ~DragHandler() = default;
  [[nodiscard]] virtual std::string_view name() const = 0;
  // 返回非空 = 由我接管这一下按下。管理器随后调 on_start()。
  [[nodiscard]] virtual std::unique_ptr<Drag> on_press(const PointerEvent&, DragContext&) {
    return nullptr;
  }
};

}  // namespace tamias
