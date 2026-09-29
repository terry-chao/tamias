#pragma once

#include "engine/interaction/input_event.h"

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <variant>

namespace tamias {

// 交互层看到的一个事件。用 variant 区分三类输入，不给 PointerEvent 塞 kind 字段。
using InteractionEvent = std::variant<PointerEvent, KeyEvent, WheelEvent>;

// 宿主的"等待原语"。drag 层的循环（Drag::doIt / DragManager::run_until_finished）
// 靠它拿事件，但**不规定事件从哪来**：
//   · Qt 壳：内部用 QEventLoop，把控制权交回平台泵（实现留在 app 层）；
//   · 无头 / 测试：QueuedEventSource；
//   · 将来非 Qt 的壳：PeekMessage / DispatchMessage。
//
// 契约：wait() 返回 false 只表示"这次等超时了"。若宿主已经不会再产生事件
// （窗口销毁、文档关闭），必须让 alive() 返回 false —— 否则调用方的循环会空转。
class EventSource {
 public:
  virtual ~EventSource() = default;
  virtual bool wait(InteractionEvent& out, std::chrono::milliseconds timeout) = 0;
  [[nodiscard]] virtual bool alive() const = 0;
};

// 线程安全的队列 + 条件变量。测试直接喂脚本；无头壳也可以拿它当泵。
class QueuedEventSource final : public EventSource {
 public:
  void push(InteractionEvent event);
  // 宿主结束：close() 之后仍然会先把队列里剩的事件吐完，再以 alive()==false 收尾。
  void close();
  void clear();

  bool wait(InteractionEvent& out, std::chrono::milliseconds timeout) override;
  [[nodiscard]] bool alive() const override;
  [[nodiscard]] std::size_t size() const;

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<InteractionEvent> queue_;
  bool closed_ = false;
};

}  // namespace tamias
