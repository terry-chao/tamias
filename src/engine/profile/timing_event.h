#pragma once

#include "engine/profile/timing_category.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tamias {

struct TimingEvent {
  std::string name;
  TimingCategory category = TimingCategory::Command;
  std::uint64_t start_us = 0;
  std::uint64_t duration_us = 0;
  std::uint64_t thread_id = 0;
  int parent = -1;
};

// 自身耗时：本事件时长减去所有直接子事件。叶子节点等于 duration_us。
inline std::vector<std::uint64_t> exclusive_durations(const std::vector<TimingEvent>& events) {
  std::vector<std::uint64_t> child_sum(events.size(), 0);
  for (const TimingEvent& event : events) {
    if (event.parent >= 0 && static_cast<std::size_t>(event.parent) < events.size()) {
      child_sum[static_cast<std::size_t>(event.parent)] += event.duration_us;
    }
  }
  std::vector<std::uint64_t> self(events.size(), 0);
  for (std::size_t i = 0; i < events.size(); ++i) {
    const auto kids = child_sum[i];
    const auto total = events[i].duration_us;
    self[i] = total > kids ? total - kids : 0;
  }
  return self;
}

}  // namespace tamias
