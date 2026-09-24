#include "app/base/log_buffer.h"

#include "engine/base/log.h"

#include <chrono>
#include <cstdio>
#include <deque>
#include <mutex>

namespace tamias {
namespace {

std::mutex g_mutex;
std::deque<std::string> g_lines;
std::size_t g_capacity = 400;
bool g_installed = false;

const char* level_name(LogLevel level) {
  switch (level) {
    case LogLevel::Trace:
      return "trace";
    case LogLevel::Debug:
      return "debug";
    case LogLevel::Info:
      return "info";
    case LogLevel::Warn:
      return "warn";
    case LogLevel::Error:
      return "error";
  }
  return "info";
}

std::string format_line(LogLevel level, std::string_view message) {
  const auto now = std::chrono::system_clock::now();
  const auto time = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &time);
#else
  localtime_r(&time, &tm);
#endif
  char prefix[32];
  std::snprintf(prefix, sizeof(prefix), "[%02d:%02d:%02d] [%s] ", tm.tm_hour, tm.tm_min,
                tm.tm_sec, level_name(level));
  std::string line(prefix);
  line.append(message);
  return line;
}

}  // namespace

void install_log_buffer(std::size_t capacity) {
  {
    std::scoped_lock lock(g_mutex);
    if (g_installed) {
      return;
    }
    g_installed = true;
    g_capacity = capacity == 0 ? 400 : capacity;
  }
  set_log_sink([](LogLevel level, std::string_view message) {
    // sink 在 log 的锁外被调用；这里自己加锁，避免和读侧打架。
    std::scoped_lock lock(g_mutex);
    g_lines.push_back(format_line(level, message));
    while (g_lines.size() > g_capacity) {
      g_lines.pop_front();
    }
  });
}

std::vector<std::string> recent_log_lines() {
  std::scoped_lock lock(g_mutex);
  return {g_lines.begin(), g_lines.end()};
}

}  // namespace tamias
