#pragma once

#include "engine/profile/timing_category.h"

#include <cstdint>
#include <string_view>

namespace tamias {

// 包任意代码段：TAMIAS_TIMING_SCOPE("my_hot_path", TimingCategory::Modeling);
// 未录制或该类别关闭时几乎零开销。
#define TAMIAS_TIMING_CONCAT_INNER(a, b) a##b
#define TAMIAS_TIMING_CONCAT(a, b) TAMIAS_TIMING_CONCAT_INNER(a, b)
#define TAMIAS_TIMING_SCOPE(name, category)                                         \
  const ::tamias::TimingScope TAMIAS_TIMING_CONCAT(_tamias_timing_scope_, __LINE__)( \
      (name), (category))

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
