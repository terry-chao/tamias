#pragma once

#include "engine/base/result.h"
#include "engine/interaction/drag.h"
#include "engine/interaction/event_source.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace tamias {

// 管理器内部的运行时状态 —— 也就是"dragstate"。一次最多一个。
// 它记录的是**跨 drag 通用的**交互事实：捕捉、按下点、阈值、看门狗。
// drag 自己的业务状态（起始快照、采到的点…）留在 drag 里。
struct DragState {
  // start() 路径：管理器接管所有权（命令不需要内联结果时用这个）。
  std::unique_ptr<Drag> owned;
  // doIt() 路径：管理器只借用，生命周期由调用方保证（栈对象即可）。
  Drag* borrowed = nullptr;
  DragOwnerId owner = 0;
  DragContext* context = nullptr;
  Vec2 press_pos{};
  Vec2 last_pos{};
  double press_time = 0.0;
  double last_event_time = 0.0;
  std::uint32_t pointer_id = 0;
  // true = 由指针按下起手：有"点击 vs 拖拽"阈值语义。
  // false = 命令/工具激活起手：立即进入拖拽，所有事件直接派发。
  bool press_started = false;
  bool past_threshold = false;
  bool capturing = true;
  std::function<void(DragEnd)> on_end;

  [[nodiscard]] Drag* drag() const { return owned ? owned.get() : borrowed; }
};

// 全局唯一的输入所有者：一次只有一个 drag 拿输入。
//
// 它同时是"调度器"和"状态机"：
//   Idle ──start()/指针按下──▶ Pressing（press 起手）或 Dragging（激活起手）
//   Pressing ──越过阈值──▶ Dragging
//   Pressing ──抬起──▶ end(Clicked)
//   任意 ──Esc / 失焦 / owner_destroyed / 看门狗──▶ end(...)
//
// 它**不跑事件循环**：宿主的主循环就是循环，管理器只是被壳的
// mousePressEvent / mouseMoveEvent / keyPressEvent 调用的被动分发器。
class DragManager {
 public:
  using CompletionCallback = std::function<void(DragEnd)>;
  using EndObserver = std::function<void(DragOwnerId, std::string_view, DragEnd)>;
  using ClockFn = std::function<double()>;

  // 每 UI 线程一个（和 QDragManager / FSlateApplication 一样的粒度）。
  // 构造函数保持 public：测试里直接建独立实例，避免用例之间互相污染。
  static DragManager& instance();

  DragManager();
  ~DragManager();
  DragManager(const DragManager&) = delete;
  DragManager& operator=(const DragManager&) = delete;

  // press 起手的仲裁器，注册顺序 = 优先级。
  void add_handler(std::unique_ptr<DragHandler> handler);

  // 命令 / 工具激活式：立即返回，输入从这里开始归 drag。
  // 若已有活动 drag，先以 Cancelled 结束它（新交互抢占，和 Blender / Slate 一致）。
  Result<void> start(DragOwnerId owner, DragContext& context, std::unique_ptr<Drag> drag,
                     CompletionCallback on_end = {});

  // —— 阻塞式（借用语义）——
  //
  // 把 drag 借给管理器，并驱动一次循环直到 on_end 才返回。事件从 source 来；
  // drag 的生命周期由调用方负责（栈对象即可）。管理器**不会** delete 它。
  //
  // 循环期间宿主照常把事件送进 handle_*（Qt 壳由主循环派发），source 负责
  // 在"没有事件"时阻塞、在"宿主没了"时让 alive() 返回 false。
  [[nodiscard]] Result<DragEnd> run_until_finished(
      DragContext& context, Drag& drag, EventSource& source,
      std::chrono::milliseconds poll = std::chrono::milliseconds{50});

  // 壳把翻译好的事件送进来。返回 true = 已消费，不要再走默认逻辑。
  //
  // 三个函数的消费策略刻意不同：
  //  · pointer：捕获期间**一律**消费（按下已被接管，第二个 Down 不能漏给壳）；
  //  · key / wheel：尊重 drag 返回的 Ignored，好让它落到壳的快捷键 / 缩放。
  bool handle_pointer(DragContext& context, const PointerEvent& event);
  bool handle_key(DragContext& context, const KeyEvent& event);
  bool handle_wheel(DragContext& context, const WheelEvent& event);

  // 取消 / 强制结束。reason 会原样传给 drag 的 on_end。
  void cancel(DragOwnerId owner, DragEnd reason = DragEnd::Cancelled);
  void cancel_all(DragEnd reason = DragEnd::Cancelled);
  // 视口析构时必须调：drag 不能在宿主死后继续持有引用。
  void owner_destroyed(DragOwnerId owner);
  // 换文档 / 重置会话。
  void reset();

  // 看门狗：没有任何事件时也要能超时。壳按帧调它（没有专用定时器）。
  void tick(double now_seconds);

  [[nodiscard]] bool active() const { return state_.has_value(); }
  [[nodiscard]] DragOwnerId owner() const;
  [[nodiscard]] std::string_view active_name() const;
  [[nodiscard]] bool past_threshold() const;
  [[nodiscard]] const DragState* state() const { return state_ ? &*state_ : nullptr; }

  // 观测：最后结束原因 + 累计结束次数（排查"这次点击为什么没反应"）。
  [[nodiscard]] std::optional<DragEnd> last_end() const { return last_end_; }
  [[nodiscard]] std::size_t ended_count() const { return ended_count_; }

  void set_drag_threshold(float pixels) { drag_threshold_ = pixels; }
  [[nodiscard]] float drag_threshold() const { return drag_threshold_; }
  // 0 = 关闭看门狗。
  void set_watchdog_timeout(double seconds) { watchdog_seconds_ = seconds; }
  [[nodiscard]] double watchdog_timeout() const { return watchdog_seconds_; }
  void set_end_observer(EndObserver observer) { end_observer_ = std::move(observer); }
  // 装上之后 Drag::doIt() 才能用。壳在启动时装一次；不装则 doIt() 报错。
  void set_event_source(EventSource* source) { event_source_ = source; }
  [[nodiscard]] EventSource* event_source() const { return event_source_; }
  // 时间源注入：测试用它得到确定的时间线。
  void set_clock(ClockFn clock) { clock_ = std::move(clock); }

 private:
  void activate(DragOwnerId owner, DragContext& context, std::unique_ptr<Drag> owned,
                Drag* borrowed, CompletionCallback on_end, bool press_started,
                const PointerEvent* trigger);
  // 结束并把状态清空，然后才回调（回调里可以安全地再 start 一个）。
  void finish(DragEnd reason);
  void check_watchdog(double now);
  // 没有显式设置看门狗时，阻塞循环用的兜底超时（防止宿主饿死事件导致空转）。
  [[nodiscard]] double effective_watchdog() const;
  [[nodiscard]] double now() const;
  [[nodiscard]] bool dispatch_pointer(DragState& state, const PointerEvent& event);
  [[nodiscard]] bool dispatch_event(DragContext& context, const InteractionEvent& event);

  std::optional<DragState> state_;
  std::vector<std::unique_ptr<DragHandler>> handlers_;
  std::optional<DragEnd> last_end_;
  std::size_t ended_count_ = 0;
  float drag_threshold_ = 4.f;
  double watchdog_seconds_ = 0.0;
  ClockFn clock_;
  EndObserver end_observer_;
  EventSource* event_source_ = nullptr;
};

}  // namespace tamias
