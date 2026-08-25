#pragma once

#include <cstdint>

namespace tamias {

enum class LocationKind : std::uint8_t {
  Point = 0,
  Line = 1,
  Surface = 2,
};

}  // namespace tamias
