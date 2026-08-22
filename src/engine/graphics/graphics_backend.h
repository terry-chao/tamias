#pragma once

#include <cstdint>

namespace tamias {

enum class GraphicsBackend : std::uint8_t {
  Vulkan = 0,
  OpenGL = 1,
  WebGL = 2,
};

[[nodiscard]] inline const char* to_string(GraphicsBackend backend) {
  switch (backend) {
    case GraphicsBackend::Vulkan:
      return "Vulkan";
    case GraphicsBackend::OpenGL:
      return "OpenGL";
    case GraphicsBackend::WebGL:
      return "WebGL";
  }
  return "Unknown";
}

}  // namespace tamias
