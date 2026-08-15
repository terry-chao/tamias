#pragma once

#include "engine/render/render_runtime.h"
#include "engine/graphics/graphics_backend.h"

#include <QString>

namespace tamias {

enum class UiColorScheme { System, Light, Dark };

class AppSettings {
 public:
  static AppSettings& instance();

  void load();
  void save() const;

  [[nodiscard]] GraphicsBackend graphics_backend() const { return graphics_backend_; }
  void set_graphics_backend(GraphicsBackend backend);

  [[nodiscard]] QString ui_language() const { return ui_language_; }
  void set_ui_language(const QString& language);

  [[nodiscard]] UiColorScheme ui_color_scheme() const { return ui_color_scheme_; }
  void set_ui_color_scheme(UiColorScheme scheme);

  [[nodiscard]] RenderDeviceConfig render_device_config() const;

 private:
  AppSettings() = default;

  GraphicsBackend graphics_backend_ = GraphicsBackend::Vulkan;
  QString ui_language_ = QStringLiteral("system");
  UiColorScheme ui_color_scheme_ = UiColorScheme::System;
};

void apply_ui_color_scheme(UiColorScheme scheme);

}  // namespace tamias
