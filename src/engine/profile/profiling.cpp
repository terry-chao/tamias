#include "engine/profile/profiling.h"

#if defined(TAMIAS_ENABLE_TRACY)

#include <cstring>

namespace tamias::profiling {
namespace {

const char* safe_text(std::string_view text) noexcept {
  return text.empty() ? "" : text.data();
}

}  // namespace

void set_program_name(const char* name) noexcept { TracySetProgramName(name); }

void set_thread_name(const char* name) noexcept { tracy::SetThreadName(name); }

void frame_mark() noexcept { FrameMark; }

void plot_i64(const char* name, std::int64_t value) noexcept { TracyPlot(name, value); }

void plot_f64(const char* name, double value) noexcept { TracyPlot(name, value); }

bool connected() noexcept { return tracy::GetProfiler().IsConnected(); }

DynamicZone::DynamicZone(std::string_view name, TimingCategory category, const char* file,
                         const char* function)
    : zone_(0, file, std::strlen(file), function, std::strlen(function), safe_text(name),
            name.size(), zone_color(category), 0, true) {}

DynamicZone::~DynamicZone() = default;

}  // namespace tamias::profiling

#else

namespace tamias::profiling {

void set_program_name(const char*) noexcept {}

void set_thread_name(const char*) noexcept {}

void frame_mark() noexcept {}

void plot_i64(const char*, std::int64_t) noexcept {}

void plot_f64(const char*, double) noexcept {}

bool connected() noexcept { return false; }

}  // namespace tamias::profiling

#endif
