#include "app_settings.h"

#include "i18n.h"

#include <QGuiApplication>
#include <QSettings>
#include <QString>
#include <QStyleHints>

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

QString color_scheme_to_key(UiColorScheme scheme) {
  switch (scheme) {
    case UiColorScheme::Light:
      return QStringLiteral("light");
    case UiColorScheme::Dark:
      return QStringLiteral("dark");
    case UiColorScheme::System:
    default:
      return QStringLiteral("system");
  }
}

UiColorScheme color_scheme_from_key(const QString& key) {
  if (key.compare(QStringLiteral("light"), Qt::CaseInsensitive) == 0) {
    return UiColorScheme::Light;
  }
  if (key.compare(QStringLiteral("dark"), Qt::CaseInsensitive) == 0) {
    return UiColorScheme::Dark;
  }
  return UiColorScheme::System;
}

QString normalize_ui_language_preference(const QString& key) {
  if (key.isEmpty() || key.compare(system_ui_language(), Qt::CaseInsensitive) == 0 ||
      key.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0) {
    return system_ui_language();
  }
  if (key.compare(QStringLiteral("en"), Qt::CaseInsensitive) == 0 ||
      key.startsWith(QStringLiteral("en_"), Qt::CaseInsensitive)) {
    return QStringLiteral("en");
  }
  if (key.compare(QStringLiteral("zh"), Qt::CaseInsensitive) == 0 ||
      key.startsWith(QStringLiteral("zh_"), Qt::CaseInsensitive) ||
      key.compare(QStringLiteral("zh-CN"), Qt::CaseInsensitive) == 0) {
    return QStringLiteral("zh_CN");
  }
  if (available_ui_languages().contains(key)) {
    return key;
  }
  return default_ui_language();
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
  ui_language_ = normalize_ui_language_preference(
      settings.value(QStringLiteral("ui/language"), default_ui_language()).toString());
  ui_color_scheme_ = color_scheme_from_key(
      settings.value(QStringLiteral("ui/color_scheme"), QStringLiteral("system")).toString());
}

void AppSettings::save() const {
  QSettings settings;
  settings.setValue(QStringLiteral("render/backend"), backend_to_key(graphics_backend_));
  settings.setValue(QStringLiteral("ui/language"), ui_language_);
  settings.setValue(QStringLiteral("ui/color_scheme"), color_scheme_to_key(ui_color_scheme_));
}

void AppSettings::set_graphics_backend(GraphicsBackend backend) {
  graphics_backend_ = backend;
}

void AppSettings::set_ui_language(const QString& language) {
  ui_language_ = normalize_ui_language_preference(language);
}

void AppSettings::set_ui_color_scheme(UiColorScheme scheme) {
  ui_color_scheme_ = scheme;
}

RenderDeviceConfig AppSettings::render_device_config() const {
  RenderDeviceConfig config{};
  config.backend = graphics_backend_;
  config.enable_validation = true;
  return config;
}

void apply_ui_color_scheme(UiColorScheme scheme) {
  QStyleHints* hints = QGuiApplication::styleHints();
  if (!hints) {
    return;
  }
  switch (scheme) {
    case UiColorScheme::Light:
      hints->setColorScheme(Qt::ColorScheme::Light);
      break;
    case UiColorScheme::Dark:
      hints->setColorScheme(Qt::ColorScheme::Dark);
      break;
    case UiColorScheme::System:
    default:
      hints->setColorScheme(Qt::ColorScheme::Unknown);
      break;
  }
}

}  // namespace tamias
