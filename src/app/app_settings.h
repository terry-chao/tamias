#pragma once

#include "engine/render/render_runtime.h"
#include "engine/graphics/graphics_backend.h"

#include <QString>
#include <QStringList>

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

  [[nodiscard]] bool zoom_to_mouse_position() const { return zoom_to_mouse_position_; }
  void set_zoom_to_mouse_position(bool enabled);

  [[nodiscard]] QStringList disabled_plugin_ids() const {
    return disabled_plugin_ids_;
  }
  void set_disabled_plugin_ids(const QStringList& ids);
  [[nodiscard]] QStringList ribbon_command_order() const {
    return ribbon_command_order_;
  }
  void set_ribbon_command_order(const QStringList& ids);
  [[nodiscard]] QStringList hidden_plugin_ids() const {
    return disabled_plugin_ids();
  }
  void set_hidden_plugin_ids(const QStringList& ids) {
    set_disabled_plugin_ids(ids);
  }

  [[nodiscard]] RenderDeviceConfig render_device_config() const;

 private:
  AppSettings() = default;

  GraphicsBackend graphics_backend_ = GraphicsBackend::Vulkan;
  QString ui_language_ = QStringLiteral("system");
  UiColorScheme ui_color_scheme_ = UiColorScheme::System;
  bool zoom_to_mouse_position_ = true;
  QStringList disabled_plugin_ids_;
  QStringList ribbon_command_order_;
};

void apply_ui_color_scheme(UiColorScheme scheme);

}  // namespace tamias
