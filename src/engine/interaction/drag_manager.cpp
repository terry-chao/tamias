#include "engine/interaction/drag_manager.h"

#include "engine/base/log.h"

#include <chrono>
#include <string>
#include <type_traits>
#include <utility>

namespace tamias {
namespace {

double steady_now() {
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

// 阻塞循环的兜底看门狗：没设过就用这个，避免宿主不产事件时空转。
constexpr double kDefaultRunWatchdogSeconds = 30.0;

// 事件没带时间戳时（壳没填）退回单调时钟。
double event_time_or_now(double event_time, double fallback) {
  return event_time > 0.0 ? event_time : fallback;
}

}  // namespace

DragManager& DragManager::instance() {
  // 每 UI 线程一个，和 QDragManager / FSlateApplication 同样的粒度。
  // thread_local 顺带保证了"只能在创建它的线程上用"。
  thread_local DragManager manager;
  return manager;
}

DragManager::DragManager() = default;

DragManager::~DragManager() {
  // 进程退出时不能用 log/回调做重活，这里只做清理，不通知。
  state_.reset();
}

void DragManager::add_handler(std::unique_ptr<DragHandler> handler) {
  if (handler) {
    handlers_.push_back(std::move(handler));
  }
}

Result<void> DragManager::start(DragOwnerId owner, DragContext& context,
                                std::unique_ptr<Drag> drag, CompletionCallback on_end) {
  if (!drag) {
    return Err("DragManager::start: null drag");
  }
  if (!context.alive()) {
    return Err("DragManager::start: owner is not alive");
  }
  if (state_) {
    // 新交互抢占：先干净地结束旧的，和 Blender 的 modal operator、
    // Slate 的 FReply 处理一致。
    log_warn(std::string("drag '") + std::string(active_name()) + "' preempted by '" +
             std::string(drag->name()) + "'");
    finish(DragEnd::Cancelled);
  }
  activate(owner, context, std::move(drag), nullptr, std::move(on_end),
           /*press_started=*/false, nullptr);
  return {};
}

void DragManager::activate(DragOwnerId owner, DragContext& context, std::unique_ptr<Drag> owned,
                           Drag* borrowed, CompletionCallback on_end, bool press_started,
                           const PointerEvent* trigger) {
  state_.emplace();
  DragState& state = *state_;
  state.owned = std::move(owned);
  state.borrowed = borrowed;
  state.owner = owner;
  state.context = &context;
  state.on_end = std::move(on_end);
  state.press_started = press_started;
  state.capturing = true;

  const double t = trigger ? event_time_or_now(trigger->time_seconds, now()) : now();
  state.last_event_time = t;
  state.press_time = t;

  if (trigger) {
    state.press_pos = trigger->pos;
    state.last_pos = trigger->pos;
    state.pointer_id = trigger->pointer_id;
  }
  // 命令激活式：没有"点击 vs 拖拽"的犹豫期，直接进入拖拽。
  state.past_threshold = !press_started;

  // 新一轮拖拽从干净的画布开始：上一轮的橡皮筋不能留在屏幕上。
  context.clear_drag_preview();
  state.drag()->on_start(context);
  // 清预览必须配一次重绘：渲染线程会一直重画最后一帧，不重画的话
  // 上一轮的橡皮筋会停在屏幕上。
  context.request_redraw();

  if (trigger) {
    // press 起手：把这一下按下也交给 drag，让它记自己的起始快照。
    const InteractionResult r = state.drag()->on_pointer(*trigger);
    if (r.status == EventStatus::Finished) {
      finish(DragEnd::Committed);
    } else if (r.status == EventStatus::Cancelled) {
      finish(DragEnd::Cancelled);
    }
  }
}

Result<DragEnd> DragManager::run_until_finished(DragContext& context, Drag& drag,
                                                EventSource& source,
                                                std::chrono::milliseconds poll) {
  if (!context.alive()) {
    return Err("DragManager::run_until_finished: owner is not alive");
  }
  if (state_) {
    log_warn(std::string("drag '") + std::string(active_name()) + "' preempted by '" +
             std::string(drag.name()) + "'");
    finish(DragEnd::Cancelled);
  }
  const DragOwnerId owner = context.owner();
  activate(owner, context, nullptr, &drag, {}, /*press_started=*/false, nullptr);

  // 循环只负责"要事件 / 派发 / 判断该不该继续"。事件从哪来完全由 source 决定。
  while (state_ && state_->owner == owner) {
    InteractionEvent event;
    if (source.wait(event, poll)) {
      (void)dispatch_event(context, event);
      continue;
    }
    if (!source.alive() || !context.alive()) {
      finish(DragEnd::OwnerGone);
      break;
    }
    if (now() - state_->last_event_time > effective_watchdog()) {
      log_warn(std::string("drag '") + std::string(active_name()) +
               "' blocked-run watchdog timeout");
      finish(DragEnd::Timeout);
      break;
    }
  }
  return last_end_.value_or(DragEnd::Cancelled);
}

bool DragManager::dispatch_event(DragContext& context, const InteractionEvent& event) {
  return std::visit(
      [&](const auto& typed) -> bool {
        using T = std::decay_t<decltype(typed)>;
        if constexpr (std::is_same_v<T, PointerEvent>) {
          return handle_pointer(context, typed);
        } else if constexpr (std::is_same_v<T, KeyEvent>) {
          return handle_key(context, typed);
        } else {
          return handle_wheel(context, typed);
        }
      },
      event);
}

// Drag::doIt 的落地：借 this 给管理器，用管理器上装的 EventSource 驱动。
// 定义放在这里是因为它是唯一同时认识 Drag 和 DragManager 的地方
// （drag.h 不能 include drag_manager.h，否则成环）。
Result<DragEnd> Drag::doIt(DragContext& context) {
  DragManager& manager = DragManager::instance();
  EventSource* source = manager.event_source();
  if (source == nullptr) {
    log_error("Drag::doIt: no EventSource installed on DragManager");
    return Err("Drag::doIt: no EventSource installed");
  }
  return manager.run_until_finished(context, *this, *source);
}

bool DragManager::handle_pointer(DragContext& context, const PointerEvent& event) {
  const double t = event_time_or_now(event.time_seconds, now());
  check_watchdog(t);

  if (state_) {
    if (state_->owner != context.owner()) {
      return false;  // 别的视口的事件不归这次 drag 管
    }
    return dispatch_pointer(*state_, event);
  }

  // 没有活动 drag：只在按下时问仲裁器，看有没有 handler 接管。
  if (event.phase != PointerPhase::Down) {
    return false;
  }
  for (auto& handler : handlers_) {
    std::unique_ptr<Drag> drag = handler->on_press(event, context);
    if (!drag) {
      continue;
    }
    activate(context.owner(), context, std::move(drag), nullptr, {}, /*press_started=*/true,
             &event);
    return true;  // 这一下按下已被接管
  }
  return false;
}

bool DragManager::dispatch_pointer(DragState& state, const PointerEvent& event) {
  const double t = event_time_or_now(event.time_seconds, now());
  state.last_event_time = t;

  if (event.phase == PointerPhase::Move || event.phase == PointerPhase::Up) {
    state.last_pos = event.pos;
  }

  if (state.press_started && !state.past_threshold) {
    switch (event.phase) {
      case PointerPhase::Move:
        if (pointer_distance(event.pos, state.press_pos) < drag_threshold_) {
          return true;  // 阈值内：连预览都不给，这一下还可能是"点击"
        }
        state.past_threshold = true;
        if (Drag* drag = state.drag()) {
          drag->on_threshold_crossed(event);
        }
        break;
      case PointerPhase::Up:
        finish(DragEnd::Clicked);  // 抬起时还没越过阈值 = 点击
        return true;
      case PointerPhase::Cancel:
        finish(DragEnd::Cancelled);
        return true;
      case PointerPhase::Down:
        break;  // 拖拽中又按了一下：原样派发，由 drag 自己决定
    }
  }

  Drag* drag = state.drag();
  if (drag == nullptr) {
    return true;  // 防御：正常路径下 finish() 已经清空
  }
  const InteractionResult r = drag->on_pointer(event);
  switch (r.status) {
    case EventStatus::Finished:
      finish(DragEnd::Committed);
      break;
    case EventStatus::Cancelled:
      finish(DragEnd::Cancelled);
      break;
    case EventStatus::Ignored:
    case EventStatus::Consumed:
      break;
  }
  // 捕获期间一律消费：按下已经被接管，不能有事件漏回壳里再触发别的交互。
  return true;
}

bool DragManager::handle_key(DragContext& context, const KeyEvent& event) {
  const double t = event_time_or_now(event.time_seconds, now());
  check_watchdog(t);

  if (!state_ || state_->owner != context.owner()) {
    return false;
  }
  state_->last_event_time = t;

  const InteractionResult r = state_->drag()->on_key(event);
  switch (r.status) {
    case EventStatus::Finished:
      finish(DragEnd::Committed);
      return true;
    case EventStatus::Cancelled:
      finish(DragEnd::Cancelled);
      return true;
    case EventStatus::Consumed:
      return true;
    case EventStatus::Ignored:
      break;
  }
  return false;  // 没消费：让壳的快捷键继续生效
}

bool DragManager::handle_wheel(DragContext& context, const WheelEvent& event) {
  const double t = event_time_or_now(event.time_seconds, now());
  check_watchdog(t);

  if (!state_ || state_->owner != context.owner()) {
    return false;
  }
  state_->last_event_time = t;

  const InteractionResult r = state_->drag()->on_wheel(event);
  switch (r.status) {
    case EventStatus::Finished:
      finish(DragEnd::Committed);
      return true;
    case EventStatus::Cancelled:
      finish(DragEnd::Cancelled);
      return true;
    case EventStatus::Consumed:
      return true;
    case EventStatus::Ignored:
      break;
  }
  return false;
}

void DragManager::cancel(DragOwnerId owner, DragEnd reason) {
  if (!state_) {
    return;
  }
  if (owner != 0 && state_->owner != owner) {
    return;
  }
  finish(reason);
}

void DragManager::cancel_all(DragEnd reason) { finish(reason); }

void DragManager::owner_destroyed(DragOwnerId owner) { cancel(owner, DragEnd::OwnerGone); }

void DragManager::reset() {
  finish(DragEnd::Cancelled);
  state_.reset();
  last_end_.reset();
  ended_count_ = 0;
}

void DragManager::tick(double now_seconds) { check_watchdog(now_seconds); }

void DragManager::check_watchdog(double current) {
  if (!state_ || watchdog_seconds_ <= 0.0) {
    return;
  }
  if (current - state_->last_event_time <= watchdog_seconds_) {
    return;
  }
  log_warn(std::string("drag '") + std::string(active_name()) +
           "' watchdog timeout; force-cancelling");
  finish(DragEnd::Timeout);
}

void DragManager::finish(DragEnd reason) {
  if (!state_) {
    return;
  }
  // 先把状态摘出来再回调：回调里可以安全地再 start 一个 drag（重入安全）。
  DragState state = std::move(*state_);
  state_.reset();

  last_end_ = reason;
  ++ended_count_;

  Drag* drag = state.drag();
  // 先通知 drag 收尾（它可能还要推最后一次预览），再由管理器统一清干净。
  if (drag != nullptr) {
    drag->on_end(reason);  // 提交 / 回滚都发生在这里
  }
  if (state.context != nullptr) {
    state.context->clear_drag_preview();
    // 同理：清预览 + 请求重绘，缺一个就会留下残影（取消拖拽时最明显）。
    state.context->request_redraw();
  }
  if (state.on_end) {
    state.on_end(reason);
  }
  if (end_observer_ && drag != nullptr) {
    end_observer_(state.owner, drag->name(), reason);
  }
}

double DragManager::now() const { return clock_ ? clock_() : steady_now(); }

double DragManager::effective_watchdog() const {
  return watchdog_seconds_ > 0.0 ? watchdog_seconds_ : kDefaultRunWatchdogSeconds;
}

DragOwnerId DragManager::owner() const { return state_ ? state_->owner : 0; }

std::string_view DragManager::active_name() const {
  const Drag* drag = state_ ? state_->drag() : nullptr;
  return drag != nullptr ? drag->name() : std::string_view{};
}

bool DragManager::past_threshold() const { return state_ && state_->past_threshold; }

}  // namespace tamias
