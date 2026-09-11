#pragma once

#include "engine/math/math.h"

#include <cstdint>

namespace tamias {

// One BRep Face after tessellation: index range into MeshCpu plus local AABB.
struct MeshFaceRange {
  std::uint32_t first_index = 0;
  std::uint32_t index_count = 0;
  Aabb bounds{};
};

}  // namespace tamias
