#pragma once

#include <cstdint>

namespace tamias {

struct HostPickPoint {
  float x = 0.f;
  float y = 0.f;
  float z = 0.f;
  std::uint32_t reserved = 0;
  std::uint64_t entity_id = 0;
};

static_assert(sizeof(HostPickPoint) == 24);

}  // namespace tamias
