#include "app/base/app_settings.h"

#include "app/base/i18n.h"

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

QString normalize_ribbon_style(const QString& key) {
  return key.compare(QStringLiteral("icons"), Qt::CaseInsensitive) == 0
             ? QStringLiteral("icons")
             : QStringLiteral("text");
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
  kernel_backend_ =
      settings.value(QStringLiteral("modeling/kernel"), QStringLiteral("occt")).toString();
  ui_language_ = normalize_ui_language_preference(
      settings.value(QStringLiteral("ui/language"), default_ui_language()).toString());
  ui_color_scheme_ = color_scheme_from_key(
      settings.value(QStringLiteral("ui/color_scheme"), QStringLiteral("system")).toString());
  zoom_to_mouse_position_ =
      settings.value(QStringLiteral("viewport/zoom_to_mouse_position"), true).toBool();
  ribbon_style_ = normalize_ribbon_style(
      settings.value(QStringLiteral("ui/ribbon_style"), QStringLiteral("text")).toString());
  ribbon_floating_groups_ =
      settings.value(QStringLiteral("ui/ribbon_floating_groups")).toStringList();
  ribbon_layout_ = settings.value(QStringLiteral("ui/ribbon_layout")).toStringList();
  ribbon_collapsed_ = settings.value(QStringLiteral("ui/ribbon_collapsed"), false).toBool();
  window_state_ = settings.value(QStringLiteral("ui/window_state")).toByteArray();
  const QString disabled_key = QStringLiteral("plugins/disabled_ids");
  disabled_plugin_ids_ =
      settings.contains(disabled_key)
          ? settings.value(disabled_key).toStringList()
          : settings.value(QStringLiteral("plugins/hidden_ids")).toStringList();
  ribbon_command_order_ =
      settings.value(QStringLiteral("plugins/ribbon_command_order")).toStringList();
  // 上次跑通的后端（含 GPU 指纹）：没有就是空——首次运行 / 换了机器。
  const QString last_good = settings.value(QStringLiteral("render/last_good_backend")).toString();
  last_good_backend_ = last_good.isEmpty() ? std::nullopt
                                           : std::optional<GraphicsBackend>(backend_from_key(last_good));
  last_good_gpu_fingerprint_ =
      settings.value(QStringLiteral("render/last_good_gpu")).toString();
}

void AppSettings::save() const {
  QSettings settings;
  settings.setValue(QStringLiteral("render/backend"), backend_to_key(graphics_backend_));
  settings.setValue(QStringLiteral("modeling/kernel"), kernel_backend_);
  settings.setValue(QStringLiteral("ui/language"), ui_language_);
  settings.setValue(QStringLiteral("ui/color_scheme"), color_scheme_to_key(ui_color_scheme_));
  settings.setValue(QStringLiteral("viewport/zoom_to_mouse_position"), zoom_to_mouse_position_);
  settings.setValue(QStringLiteral("ui/ribbon_style"), ribbon_style_);
  if (ribbon_floating_groups_.isEmpty()) {
    settings.remove(QStringLiteral("ui/ribbon_floating_groups"));
  } else {
    settings.setValue(QStringLiteral("ui/ribbon_floating_groups"), ribbon_floating_groups_);
  }
  if (ribbon_layout_.isEmpty()) {
    settings.remove(QStringLiteral("ui/ribbon_layout"));
  } else {
    settings.setValue(QStringLiteral("ui/ribbon_layout"), ribbon_layout_);
  }
  settings.setValue(QStringLiteral("ui/ribbon_collapsed"), ribbon_collapsed_);
  if (window_state_.isEmpty()) {
    settings.remove(QStringLiteral("ui/window_state"));
  } else {
    settings.setValue(QStringLiteral("ui/window_state"), window_state_);
  }
  settings.setValue(QStringLiteral("plugins/disabled_ids"), disabled_plugin_ids_);
  settings.setValue(QStringLiteral("plugins/ribbon_command_order"),
                    ribbon_command_order_);
  if (last_good_backend_.has_value()) {
    settings.setValue(QStringLiteral("render/last_good_backend"),
                      backend_to_key(*last_good_backend_));
    settings.setValue(QStringLiteral("render/last_good_gpu"), last_good_gpu_fingerprint_);
  }
  settings.remove(QStringLiteral("plugins/hidden_ids"));
}

void AppSettings::set_graphics_backend(GraphicsBackend backend) {
  graphics_backend_ = backend;
}

void AppSettings::set_kernel_backend(const QString& backend) {
  kernel_backend_ = backend.isEmpty() ? QStringLiteral("occt") : backend;
}

void AppSettings::set_ui_language(const QString& language) {
  ui_language_ = normalize_ui_language_preference(language);
}

void AppSettings::set_ui_color_scheme(UiColorScheme scheme) {
  ui_color_scheme_ = scheme;
}

void AppSettings::set_zoom_to_mouse_position(bool enabled) {
  zoom_to_mouse_position_ = enabled;
}

void AppSettings::set_ribbon_style(const QString& style) {
  ribbon_style_ = normalize_ribbon_style(style);
}

void AppSettings::set_ribbon_floating_groups(const QStringList& entries) {
  ribbon_floating_groups_ = entries;
}

void AppSettings::set_ribbon_layout(const QStringList& entries) { ribbon_layout_ = entries; }

void AppSettings::set_ribbon_collapsed(bool collapsed) { ribbon_collapsed_ = collapsed; }

void AppSettings::set_window_state(const QByteArray& state) { window_state_ = state; }

void AppSettings::set_disabled_plugin_ids(const QStringList& ids) {
  disabled_plugin_ids_ = ids;
}

void AppSettings::set_ribbon_command_order(const QStringList& ids) {
  ribbon_command_order_ = ids;
}

RenderDeviceConfig AppSettings::render_device_config() const {
  RenderDeviceConfig config{};
  config.backend = resolved_backend();
  // 安全模式关掉校验层：校验层自己也要加载驱动代码，少一层少一份风险。
  config.enable_validation = !safe_mode_;
  return config;
}

GraphicsBackend AppSettings::resolved_backend() const {
  return resolved_backend_.value_or(graphics_backend_);
}

void AppSettings::set_last_good_backend(GraphicsBackend backend, const QString& fingerprint) {
  last_good_backend_ = backend;
  last_good_gpu_fingerprint_ = fingerprint;
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
