#pragma once

#include "engine/interaction/event_source.h"

#include <QEventLoop>

#include <chrono>

namespace tamias {

// Qt 壳的"等待原语"，装到当前 UI 线程的 DragManager 上。
//
// 注意这里的分工：**事件不经过它**。阻塞式 drag 的事件仍然是 Qt 正常派发到
// 视口、再由视口送进 DragManager::handle_* 直达 drag；这个类只负责
// `run_until_finished` 的循环里"把控制权交回平台泵，并睡到有事发生"。
//
// 所以 wait() 几乎总是返回 false（= "睡了一觉"），循环回去重新检查还有没有
// 活动 drag。它也不需要队列——排队那套是给无头 / 测试用的 QueuedEventSource。
//
// 线程约束：只能在 UI 线程调用（DragManager 本身就是每 UI 线程一个）。
class QtEventSource final : public EventSource {
 public:
  bool wait(InteractionEvent& out, std::chrono::milliseconds timeout) override;
  [[nodiscard]] bool alive() const override;

  // 让正在 wait() 的循环立刻返回（drag 结束时管理器会调）。
  void wake();
  // 壳要退出时调：之后 alive() 为 false，阻塞中的循环会以 OwnerGone 收尾。
  void close();

 private:
  QEventLoop* loop_ = nullptr;
  bool closed_ = false;
};

// 把 QtEventSource 装到当前 UI 线程的 DragManager 上，并把 drag 结束事件接到
// wake()。启动时调一次即可（和 register_linked_rhi_backends 放在一起）。
void install_qt_event_source();

}  // namespace tamias
