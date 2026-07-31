#pragma once

#include <memory>
#include <string_view>

namespace tamias {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

void init_logging(LogLevel level = LogLevel::Info);
void shutdown_logging();

void log_trace(std::string_view msg);
void log_debug(std::string_view msg);
void log_info(std::string_view msg);
void log_warn(std::string_view msg);
void log_error(std::string_view msg);

}  // namespace tamias
