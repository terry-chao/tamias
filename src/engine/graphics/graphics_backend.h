#pragma once

#include <cstdint>

namespace tamias {

enum class GraphicsBackend : std::uint8_t {
  Vulkan = 0,
  OpenGL = 1,
};

[[nodiscard]] inline const char* to_string(GraphicsBackend backend) {
  switch (backend) {
    case GraphicsBackend::Vulkan:
      return "Vulkan";
    case GraphicsBackend::OpenGL:
      return "OpenGL";
  }
  return "Unknown";
}

}  // namespace tamias
