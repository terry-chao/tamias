#pragma once

#include "engine/profile/timing_category.h"
#include "engine/profile/timing_event.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tamias {

class TimingSession {
 public:
  [[nodiscard]] static TimingSession& instance();

  void start();
  void stop();
  void clear();

  [[nodiscard]] bool is_recording() const {
    return recording_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool category_enabled(TimingCategory category) const;
  void set_category_enabled(TimingCategory category, bool enabled);

  [[nodiscard]] int begin_event(std::string_view name, TimingCategory category);
  void end_event(int index, std::uint32_t generation);

  [[nodiscard]] std::uint32_t generation() const {
    return generation_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] std::uint64_t elapsed_us() const;
  [[nodiscard]] std::uint64_t duration_us() const;
  [[nodiscard]] std::string started_utc() const;
  [[nodiscard]] std::vector<TimingEvent> events() const;

 private:
  TimingSession();

  [[nodiscard]] static std::uint32_t default_category_mask();
  [[nodiscard]] static std::uint64_t current_thread_id();

  std::atomic<bool> recording_{false};
  std::atomic<std::uint32_t> generation_{0};
  std::atomic<std::uint32_t> category_mask_{0};
  std::atomic<std::uint64_t> start_us_{0};
  std::atomic<std::uint64_t> stop_us_{0};

  mutable std::mutex mutex_;
  std::vector<TimingEvent> events_;
  std::string started_utc_;
};

}  // namespace tamias
