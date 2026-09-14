#pragma once

#include "engine/profile/timing_category.h"

#include <cstdint>
#include <string_view>

#if defined(TAMIAS_ENABLE_TRACY)
#include <tracy/Tracy.hpp>
#endif

// 唯一变量名拼接（本文件和 timing_scope.h 共用）。
#define TAMIAS_TIMING_CONCAT_INNER(a, b) a##b
#define TAMIAS_TIMING_CONCAT(a, b) TAMIAS_TIMING_CONCAT_INNER(a, b)

namespace tamias::profiling {

// Tracy 时间线上的类别配色（0xAARRGGBB），和 TimingPanel 的芯片色一致。
[[nodiscard]] constexpr std::uint32_t zone_color(TimingCategory category) noexcept {
  switch (category) {
    case TimingCategory::Command:
      return 0xFF2F7DDE;
    case TimingCategory::Modeling:
      return 0xFFD67E2C;
    case TimingCategory::Render:
      return 0xFF2EA06E;
    case TimingCategory::Ui:
      return 0xFF9A6AD6;
  }
  return 0xFF9AA0A6;
}

// 进程名 / 线程名 / 帧标记 / 自定义曲线。没编 Tracy 时全是空实现。
// plot_* 的名字必须是字符串字面量：Tracy 只把指针写进队列。
void set_program_name(const char* name) noexcept;
void set_thread_name(const char* name) noexcept;
void frame_mark() noexcept;
void plot_i64(const char* name, std::int64_t value) noexcept;
void plot_f64(const char* name, double value) noexcept;
[[nodiscard]] bool connected() noexcept;

#if defined(TAMIAS_ENABLE_TRACY)

// 运行时名字（std::string / const char*）的 zone。Tracy 会把名字拷进自己的缓冲，
// 所以传 string_view 是安全的。字面量名字请用 TAMIAS_TIMING_SCOPE，走零分配路径。
class DynamicZone {
 public:
  DynamicZone(std::string_view name, TimingCategory category, const char* file,
              const char* function);
  ~DynamicZone();

  DynamicZone(const DynamicZone&) = delete;
  DynamicZone& operator=(const DynamicZone&) = delete;
  DynamicZone(DynamicZone&&) = delete;
  DynamicZone& operator=(DynamicZone&&) = delete;

 private:
  tracy::ScopedZone zone_;
};

#endif

}  // namespace tamias::profiling

// name 必须是字符串字面量：Tracy 把它存进 constexpr SourceLocationData。
#if defined(TAMIAS_ENABLE_TRACY)
#define TAMIAS_PROFILE_ZONE(name, category)                                                    \
  ZoneNamedNC(TAMIAS_TIMING_CONCAT(_tamias_tracy_zone_, __LINE__), name,                       \
              ::tamias::profiling::zone_color(category), true)
#define TAMIAS_PROFILE_DYNAMIC_ZONE(name, category)                                            \
  const ::tamias::profiling::DynamicZone TAMIAS_TIMING_CONCAT(_tamias_tracy_zone_, __LINE__)(  \
      (name), (category), __FILE__, __FUNCTION__)
#else
#define TAMIAS_PROFILE_ZONE(name, category) ((void)0)
#define TAMIAS_PROFILE_DYNAMIC_ZONE(name, category) ((void)0)
#endif

#define TAMIAS_PROFILE_FRAME_MARK() ::tamias::profiling::frame_mark()
#define TAMIAS_PROFILE_THREAD_NAME(name) ::tamias::profiling::set_thread_name(name)
#define TAMIAS_PROFILE_PLOT_INT(name, value) ::tamias::profiling::plot_i64((name), (value))
#define TAMIAS_PROFILE_PLOT_FLOAT(name, value) ::tamias::profiling::plot_f64((name), (value))
