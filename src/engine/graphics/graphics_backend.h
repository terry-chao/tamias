#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace tamias {

enum class GraphicsBackend : std::uint8_t {
  Vulkan = 0,
  OpenGL = 1,
  WebGL = 2,
  WebGPU = 3,
};

[[nodiscard]] inline const char* to_string(GraphicsBackend backend) {
  switch (backend) {
    case GraphicsBackend::Vulkan:
      return "Vulkan";
    case GraphicsBackend::OpenGL:
      return "OpenGL";
    case GraphicsBackend::WebGL:
      return "WebGL";
    case GraphicsBackend::WebGPU:
      return "WebGPU";
  }
  return "Unknown";
}

// 反查（命令行 / 块名单 / 策略文件用）。认不出返回 nullopt——策略文件写错了
// 不该默默变成别的后端。
[[nodiscard]] inline std::optional<GraphicsBackend> try_backend_from_name(std::string_view name) {
  if (name == "vulkan" || name == "Vulkan" || name == "VULKAN") {
    return GraphicsBackend::Vulkan;
  }
  if (name == "opengl" || name == "OpenGL" || name == "OPENGL" || name == "gl") {
    return GraphicsBackend::OpenGL;
  }
  return std::nullopt;
}

}  // namespace tamias
