#pragma once

#include <cstdint>
#include <string>

namespace tamias {

struct PluginPointInputRequest {
  static constexpr std::int32_t kAllowConfirm = 1 << 0;
  static constexpr std::int32_t kGridSnap = 1 << 1;
  static constexpr std::int32_t kPickEntities = 1 << 2;
  static constexpr std::int32_t kEntitiesOnly = 1 << 3;

  std::uint64_t request_id = 0;
  int min_points = 1;
  int max_points = 1;
  std::int32_t flags = 0;
  float work_plane_y = 0.f;
  int preview_kind = 0;
  std::string preview_curve_kind;
  std::string filter_kind;
};

}  // namespace tamias
