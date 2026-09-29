#include "engine/interaction/event_source.h"

#include <utility>

namespace tamias {

void QueuedEventSource::push(InteractionEvent event) {
  {
    std::scoped_lock lock(mutex_);
    if (closed_) {
      return;
    }
    queue_.push_back(std::move(event));
  }
  cv_.notify_one();
}

void QueuedEventSource::close() {
  {
    std::scoped_lock lock(mutex_);
    closed_ = true;
  }
  cv_.notify_all();
}

void QueuedEventSource::clear() {
  std::scoped_lock lock(mutex_);
  queue_.clear();
}

bool QueuedEventSource::wait(InteractionEvent& out, std::chrono::milliseconds timeout) {
  std::unique_lock lock(mutex_);
  const bool signaled = cv_.wait_for(lock, timeout, [&] { return !queue_.empty() || closed_; });
  if (!signaled || queue_.empty()) {
    return false;  // 超时，或已关闭且队列吐空
  }
  out = std::move(queue_.front());
  queue_.pop_front();
  return true;
}

bool QueuedEventSource::alive() const {
  std::scoped_lock lock(mutex_);
  return !closed_;
}

std::size_t QueuedEventSource::size() const {
  std::scoped_lock lock(mutex_);
  return queue_.size();
}

}  // namespace tamias
