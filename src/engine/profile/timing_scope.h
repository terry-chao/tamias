#pragma once

#include "engine/profile/profiling.h"

#include <cstdint>
#include <string_view>

namespace tamias {

class TimingScope {
 public:
  TimingScope(std::string_view name, TimingCategory category);
  ~TimingScope();

  TimingScope(const TimingScope&) = delete;
  TimingScope& operator=(const TimingScope&) = delete;
  TimingScope(TimingScope&&) = delete;
  TimingScope& operator=(TimingScope&&) = delete;

 private:
  int index_ = -1;
  std::uint32_t generation_ = 0;
};

}  // namespace tamias

// 包任意代码段：TAMIAS_TIMING_SCOPE("my_hot_path", TimingCategory::Modeling);
// 未录制或该类别关闭时几乎零开销。编进 Tracy 时同一条宏还会发一个同名的
// Tracy zone（颜色按类别），所以埋点只需要写一次。
//
// 名字必须是字符串字面量（Tracy 走零分配的静态路径）；运行时字符串用
// TAMIAS_TIMING_SCOPE_DYNAMIC，它会把名字拷进 Tracy 的缓冲。
#define TAMIAS_TIMING_SCOPE(name, category)                                         \
  const ::tamias::TimingScope TAMIAS_TIMING_CONCAT(_tamias_timing_scope_, __LINE__)( \
      (name), (category));                                                          \
  TAMIAS_PROFILE_ZONE(name, category)

#define TAMIAS_TIMING_SCOPE_DYNAMIC(name, category)                                 \
  const ::tamias::TimingScope TAMIAS_TIMING_CONCAT(_tamias_timing_scope_, __LINE__)( \
      (name), (category));                                                          \
  TAMIAS_PROFILE_DYNAMIC_ZONE(name, category)
