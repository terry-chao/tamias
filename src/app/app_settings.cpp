#include "app_settings.h"

#include <QSettings>
#include <QString>

namespace tamias {
namespace {

QString backend_to_key(GraphicsBackend backend) {
  switch (backend) {
    case GraphicsBackend::OpenGL:
      return QStringLiteral("OpenGL");
    case GraphicsBackend::Vulkan:
    default:
      return QStringLiteral("Vulkan");
  }
}

GraphicsBackend backend_from_key(const QString& key) {
  if (key.compare(QStringLiteral("OpenGL"), Qt::CaseInsensitive) == 0) {
    return GraphicsBackend::OpenGL;
  }
  return GraphicsBackend::Vulkan;
}

}  // namespace

AppSettings& AppSettings::instance() {
  static AppSettings settings;
  return settings;
}

void AppSettings::load() {
  QSettings settings;
  graphics_backend_ =
      backend_from_key(settings.value(QStringLiteral("render/backend"), QStringLiteral("Vulkan"))
                           .toString());
}

void AppSettings::save() const {
  QSettings settings;
  settings.setValue(QStringLiteral("render/backend"), backend_to_key(graphics_backend_));
}

void AppSettings::set_graphics_backend(GraphicsBackend backend) {
  graphics_backend_ = backend;
}

RenderDeviceConfig AppSettings::render_device_config() const {
  RenderDeviceConfig config{};
  config.backend = graphics_backend_;
  config.enable_validation = true;
  return config;
}

}  // namespace tamias
