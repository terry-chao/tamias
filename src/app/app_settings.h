#pragma once

#include "engine/render/render_runtime.h"
#include "graphics/graphics_backend.h"

namespace tamias {

class AppSettings {
 public:
  static AppSettings& instance();

  void load();
  void save() const;

  [[nodiscard]] GraphicsBackend graphics_backend() const { return graphics_backend_; }
  void set_graphics_backend(GraphicsBackend backend);

  [[nodiscard]] RenderDeviceConfig render_device_config() const;

 private:
  AppSettings() = default;

  GraphicsBackend graphics_backend_ = GraphicsBackend::Vulkan;
};

}  // namespace tamias
