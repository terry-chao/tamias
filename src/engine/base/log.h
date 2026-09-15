#pragma once

#include <memory>
#include <functional>
#include <string_view>

namespace tamias {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

void init_logging(LogLevel level = LogLevel::Info);
void shutdown_logging();

// 日志旁路：把引擎日志接进宿主自己的 UI（web 端的错误面板、测试收集等）。
// 传空函数对象即注销。回调在写日志的线程上同步调用，别在里面做重活，
// 也别再打日志（会递归）。
void set_log_sink(std::function<void(LogLevel, std::string_view)> sink);

void log_trace(std::string_view msg);
void log_debug(std::string_view msg);
void log_info(std::string_view msg);
void log_warn(std::string_view msg);
void log_error(std::string_view msg);

}  // namespace tamias
