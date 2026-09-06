#include "engine/profile/timing_session.h"

#include "engine/profile/timing_clock.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace tamias {
namespace {

thread_local std::vector<int> t_open_events;

std::string format_utc_now() {
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char out[32];
  std::snprintf(out, sizeof(out), "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
  return out;
}

}  // namespace

TimingSession& TimingSession::instance() {
  static TimingSession session;
  return session;
}

TimingSession::TimingSession() : category_mask_{default_category_mask()} {}

std::uint32_t TimingSession::default_category_mask() {
  return (1u << static_cast<unsigned>(TimingCategory::Command)) |
         (1u << static_cast<unsigned>(TimingCategory::Modeling)) |
         (1u << static_cast<unsigned>(TimingCategory::Ui));
}

std::uint64_t TimingSession::current_thread_id() {
  return static_cast<std::uint64_t>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
}

void TimingSession::start() {
  const std::uint64_t now = TimingClock::now_us();
  const std::string utc = format_utc_now();
  std::scoped_lock lock(mutex_);
  generation_.fetch_add(1, std::memory_order_relaxed);
  events_.clear();
  t_open_events.clear();
  started_utc_ = utc;
  start_us_.store(now, std::memory_order_relaxed);
  stop_us_.store(now, std::memory_order_relaxed);
  recording_.store(true, std::memory_order_release);
}

void TimingSession::stop() {
  const std::uint64_t now = TimingClock::now_us();
  stop_us_.store(now, std::memory_order_relaxed);
  recording_.store(false, std::memory_order_release);
}

void TimingSession::clear() {
  recording_.store(false, std::memory_order_release);
  std::scoped_lock lock(mutex_);
  events_.clear();
  t_open_events.clear();
  started_utc_.clear();
  start_us_.store(0, std::memory_order_relaxed);
  stop_us_.store(0, std::memory_order_relaxed);
  generation_.fetch_add(1, std::memory_order_relaxed);
}

bool TimingSession::category_enabled(TimingCategory category) const {
  const auto bit = 1u << static_cast<unsigned>(category);
  return (category_mask_.load(std::memory_order_relaxed) & bit) != 0;
}

void TimingSession::set_category_enabled(TimingCategory category, bool enabled) {
  const auto bit = 1u << static_cast<unsigned>(category);
  if (enabled) {
    category_mask_.fetch_or(bit, std::memory_order_relaxed);
  } else {
    category_mask_.fetch_and(~bit, std::memory_order_relaxed);
  }
}

int TimingSession::begin_event(std::string_view name, TimingCategory category) {
  if (!is_recording() || !category_enabled(category)) {
    return -1;
  }
  const std::uint64_t now = TimingClock::now_us();
  const std::uint64_t origin = start_us_.load(std::memory_order_relaxed);
  std::scoped_lock lock(mutex_);
  if (!recording_.load(std::memory_order_relaxed)) {
    return -1;
  }
  TimingEvent event;
  event.name.assign(name.data(), name.size());
  event.category = category;
  event.start_us = now > origin ? now - origin : 0;
  event.thread_id = current_thread_id();
  event.parent = t_open_events.empty() ? -1 : t_open_events.back();
  const int index = static_cast<int>(events_.size());
  events_.push_back(std::move(event));
  t_open_events.push_back(index);
  return index;
}

void TimingSession::end_event(int index, std::uint32_t generation) {
  if (index < 0) {
    return;
  }
  if (generation_.load(std::memory_order_relaxed) != generation) {
    return;
  }
  const std::uint64_t now = TimingClock::now_us();
  const std::uint64_t origin = start_us_.load(std::memory_order_relaxed);
  std::scoped_lock lock(mutex_);
  if (generation_.load(std::memory_order_relaxed) != generation) {
    return;
  }
  if (index >= static_cast<int>(events_.size())) {
    return;
  }
  const std::uint64_t end_rel = now > origin ? now - origin : 0;
  auto& event = events_[static_cast<std::size_t>(index)];
  event.duration_us = end_rel > event.start_us ? end_rel - event.start_us : 0;
  if (!t_open_events.empty() && t_open_events.back() == index) {
    t_open_events.pop_back();
  }
}

std::uint64_t TimingSession::elapsed_us() const {
  const std::uint64_t start = start_us_.load(std::memory_order_relaxed);
  if (start == 0) {
    return 0;
  }
  if (is_recording()) {
    const std::uint64_t now = TimingClock::now_us();
    return now > start ? now - start : 0;
  }
  const std::uint64_t stop = stop_us_.load(std::memory_order_relaxed);
  return stop > start ? stop - start : 0;
}

std::uint64_t TimingSession::duration_us() const { return elapsed_us(); }

std::string TimingSession::started_utc() const {
  std::scoped_lock lock(mutex_);
  return started_utc_;
}

std::vector<TimingEvent> TimingSession::events() const {
  std::scoped_lock lock(mutex_);
  return events_;
}

}  // namespace tamias
