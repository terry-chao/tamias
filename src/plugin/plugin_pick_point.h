#pragma once

#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

struct PluginPickPoint {
  Vec3 position{};
  std::uint64_t entity_id = 0;
};

}  // namespace tamias
