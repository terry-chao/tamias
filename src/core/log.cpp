#include "log.h"

#include <chrono>
#include <cstdio>
#include <mutex>

namespace tamias {
namespace {

LogLevel g_level = LogLevel::Info;
std::mutex g_mutex;

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

void write_line(LogLevel level, std::string_view msg) {
  if (static_cast<int>(level) < static_cast<int>(g_level)) {
    return;
  }
  std::scoped_lock lock(g_mutex);
  const auto now = std::chrono::system_clock::now();
  const auto t = std::chrono::system_clock::to_time_t(now);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif
  std::fprintf(stderr, "[%02d:%02d:%02d] [%s] %.*s\n", tm.tm_hour, tm.tm_min, tm.tm_sec,
               level_name(level), static_cast<int>(msg.size()), msg.data());
}

}  // namespace

void init_logging(LogLevel level) { g_level = level; }
void shutdown_logging() {}

void log_trace(std::string_view msg) { write_line(LogLevel::Trace, msg); }
void log_debug(std::string_view msg) { write_line(LogLevel::Debug, msg); }
void log_info(std::string_view msg) { write_line(LogLevel::Info, msg); }
void log_warn(std::string_view msg) { write_line(LogLevel::Warn, msg); }
void log_error(std::string_view msg) { write_line(LogLevel::Error, msg); }

}  // namespace tamias
