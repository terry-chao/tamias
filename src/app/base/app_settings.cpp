#include "app/base/app_settings.h"

#include "app/base/i18n.h"
#include "engine/base/log.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStandardPaths>
#include <QString>
#include <QStyleHints>
#include <QTemporaryFile>

#include <string>

namespace tamias {
namespace {

QString g_settings_file;
QString g_layout_file;

// Windows 上 isWritable() 看的是属性位：Program Files 下的目录没有只读属性，
// 但普通用户照样写不进去。所以真写一个临时文件再让它自己删掉。
bool directory_is_writable(const QString& dir) {
  QTemporaryFile probe(QDir(dir).filePath(QStringLiteral("tamias-write-probe-XXXXXX")));
  return probe.open();
}

// 旧位置 = QSettings 的默认格式。Windows 上是注册表
// HKCU\Software\tamias\tamias，其它平台是 <AppConfigLocation>/tamias/tamias.conf。
// 只在目标 ini 还不存在时搬一次，而且**不删**旧位置——迁移失败也不至于丢东西。
void migrate_legacy_settings(const QString& ini_path) {
  QSettings legacy(QSettings::NativeFormat, QSettings::UserScope, QStringLiteral("tamias"),
                   QStringLiteral("tamias"));
  const QStringList keys = legacy.allKeys();
  if (keys.isEmpty()) {
    return;
  }
  QSettings target(ini_path, QSettings::IniFormat);
  for (const QString& key : keys) {
    target.setValue(key, legacy.value(key));
  }
  target.sync();
  log_info("Settings migrated from previous location: " + ini_path.toStdString() + " (" +
           std::to_string(keys.size()) + " keys)");
}

// 布局和窗口状态以前和偏好挤在同一份 ini 里：window_state 是 @ByteArray 转义块，
// ribbon_layout / ribbon_floating_groups 是逗号串。现在挪到 tamias-layout.ini，
// window_state 改存 base64——QSettings 见到纯 ASCII 会原样写，@ByteArray 那种
// 只有它自己认得的转义就没有了。
//
// 只在布局文件还不存在时搬一次；搬完把老键从 tamias.ini 里删掉，免得两份数据打架。
void migrate_layout_state(const QString& layout_path) {
  if (QFileInfo::exists(layout_path)) {
    return;
  }
  QSettings settings = open_settings();
  const QStringList legacy_keys = {
      QStringLiteral("ui/window_state"),
      QStringLiteral("ui/ribbon_layout"),
      QStringLiteral("ui/ribbon_floating_groups"),
      QStringLiteral("ui/dock_layout_version"),
  };
  bool has_legacy = false;
  for (const QString& key : legacy_keys) {
    has_legacy = has_legacy || settings.contains(key);
  }
  if (!has_legacy) {
    return;
  }

  QSettings layout(layout_path, QSettings::IniFormat);
  layout.setValue(QStringLiteral("layout/version"),
                  settings.value(QStringLiteral("ui/dock_layout_version"), 0).toInt());
  const QByteArray window_state = settings.value(QStringLiteral("ui/window_state")).toByteArray();
  if (!window_state.isEmpty()) {
    layout.setValue(QStringLiteral("layout/window_state"),
                    QString::fromLatin1(window_state.toBase64()));
  }
  const QStringList ribbon_layout =
      settings.value(QStringLiteral("ui/ribbon_layout")).toStringList();
  if (!ribbon_layout.isEmpty()) {
    layout.setValue(QStringLiteral("layout/ribbon_layout"), ribbon_layout);
  }
  const QStringList floating_groups =
      settings.value(QStringLiteral("ui/ribbon_floating_groups")).toStringList();
  if (!floating_groups.isEmpty()) {
    layout.setValue(QStringLiteral("layout/floating_groups"), floating_groups);
  }
  layout.sync();
  for (const QString& key : legacy_keys) {
    settings.remove(key);
  }
  settings.sync();
  log_info("Layout state moved to its own file: " + layout_path.toStdString());
}

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
  QSettings settings = open_settings();
  graphics_backend_ =
      backend_from_key(settings.value(QStringLiteral("render/backend"), QStringLiteral("Vulkan"))
                           .toString());
  kernel_backend_ =
      settings.value(QStringLiteral("modeling/kernel"), QStringLiteral("occt")).toString();
  ui_language_ = normalize_ui_language_preference(
      settings.value(QStringLiteral("ui/language"), default_ui_language()).toString());
  ui_backend_ = settings.value(QStringLiteral("ui/backend"), QStringLiteral("qt")).toString();
  if (ui_backend_.isEmpty()) {
    ui_backend_ = QStringLiteral("qt");
  }
  ui_color_scheme_ = color_scheme_from_key(
      settings.value(QStringLiteral("ui/color_scheme"), QStringLiteral("system")).toString());
  zoom_to_mouse_position_ =
      settings.value(QStringLiteral("viewport/zoom_to_mouse_position"), true).toBool();
  ribbon_style_ = normalize_ribbon_style(
      settings.value(QStringLiteral("ui/ribbon_style"), QStringLiteral("text")).toString());
  ribbon_collapsed_ = settings.value(QStringLiteral("ui/ribbon_collapsed"), false).toBool();
  ai_base_url_ = settings.value(QStringLiteral("ai/base_url")).toString();
  ai_model_ = settings.value(QStringLiteral("ai/model")).toString();
  meshy_base_url_ = settings.value(QStringLiteral("meshy/base_url")).toString();
  meshy_model_ = settings.value(QStringLiteral("meshy/model")).toString();
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

  // 布局在另一份文件里（见 init_settings_storage 的注释）。
  QSettings layout = open_layout_settings();
  ribbon_floating_groups_ = layout.value(QStringLiteral("layout/floating_groups")).toStringList();
  ribbon_layout_ = layout.value(QStringLiteral("layout/ribbon_layout")).toStringList();
  const QString stored_state = layout.value(QStringLiteral("layout/window_state")).toString();
  window_state_ = QByteArray::fromBase64(stored_state.toLatin1());
  dock_layout_version_ = layout.value(QStringLiteral("layout/version"), 0).toInt();
}

void AppSettings::save() const {
  QSettings settings = open_settings();
  settings.setValue(QStringLiteral("render/backend"), backend_to_key(graphics_backend_));
  settings.setValue(QStringLiteral("modeling/kernel"), kernel_backend_);
  settings.setValue(QStringLiteral("ui/language"), ui_language_);
  settings.setValue(QStringLiteral("ui/backend"), ui_backend_);
  settings.setValue(QStringLiteral("ui/color_scheme"), color_scheme_to_key(ui_color_scheme_));
  settings.setValue(QStringLiteral("viewport/zoom_to_mouse_position"), zoom_to_mouse_position_);
  settings.setValue(QStringLiteral("ui/ribbon_style"), ribbon_style_);
  settings.setValue(QStringLiteral("ui/ribbon_collapsed"), ribbon_collapsed_);
  settings.setValue(QStringLiteral("plugins/disabled_ids"), disabled_plugin_ids_);
  settings.setValue(QStringLiteral("plugins/ribbon_command_order"),
                    ribbon_command_order_);
  settings.setValue(QStringLiteral("ai/base_url"), ai_base_url_);
  settings.setValue(QStringLiteral("ai/model"), ai_model_);
  settings.setValue(QStringLiteral("meshy/base_url"), meshy_base_url_);
  settings.setValue(QStringLiteral("meshy/model"), meshy_model_);
  if (last_good_backend_.has_value()) {
    settings.setValue(QStringLiteral("render/last_good_backend"),
                      backend_to_key(*last_good_backend_));
    settings.setValue(QStringLiteral("render/last_good_gpu"), last_good_gpu_fingerprint_);
  }
  settings.remove(QStringLiteral("plugins/hidden_ids"));
  // 布局另存一份，但 save() 也顺带写一遍：调用方多半只关心「都落盘了」，
  // 不该因为记错方法而让刚设的布局悄悄留在内存里。
  save_layout();
}

void AppSettings::save_layout() const {
  QSettings layout = open_layout_settings();
  layout.setValue(QStringLiteral("layout/version"), dock_layout_version_);
  if (window_state_.isEmpty()) {
    layout.remove(QStringLiteral("layout/window_state"));
  } else {
    // base64：纯 ASCII，QSettings 原样写，读回来也不依赖 QDataStream 的版本。
    layout.setValue(QStringLiteral("layout/window_state"),
                    QString::fromLatin1(window_state_.toBase64()));
  }
  if (ribbon_layout_.isEmpty()) {
    layout.remove(QStringLiteral("layout/ribbon_layout"));
  } else {
    layout.setValue(QStringLiteral("layout/ribbon_layout"), ribbon_layout_);
  }
  if (ribbon_floating_groups_.isEmpty()) {
    layout.remove(QStringLiteral("layout/floating_groups"));
  } else {
    layout.setValue(QStringLiteral("layout/floating_groups"), ribbon_floating_groups_);
  }
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

void AppSettings::set_ui_backend(const QString& backend) {
  ui_backend_ = backend.isEmpty() ? QStringLiteral("qt") : backend;
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

void AppSettings::set_ai_base_url(const QString& url) { ai_base_url_ = url; }

void AppSettings::set_ai_model(const QString& model) { ai_model_ = model; }

void AppSettings::set_meshy_base_url(const QString& url) { meshy_base_url_ = url; }

void AppSettings::set_meshy_model(const QString& model) { meshy_model_ = model; }

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

const QString& app_settings_file_path() { return g_settings_file; }

const QString& app_layout_file_path() { return g_layout_file; }

QString init_settings_storage() {
  if (!g_settings_file.isEmpty()) {
    return g_settings_file;
  }

  QString dir = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("config"));
  if (!QDir().mkpath(dir) || !directory_is_writable(dir)) {
    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!fallback.isEmpty()) {
      dir = fallback;
      QDir().mkpath(dir);
      log_warn("Config directory next to the executable is not writable; using " +
               dir.toStdString());
    }
  }

  g_settings_file = QDir(dir).filePath(QStringLiteral("tamias.ini"));
  g_layout_file = QDir(dir).filePath(QStringLiteral("tamias-layout.ini"));
  if (!QFileInfo::exists(g_settings_file)) {
    migrate_legacy_settings(g_settings_file);
  }
  migrate_layout_state(g_layout_file);
  log_info("Settings file: " + g_settings_file.toStdString());
  log_info("Layout file: " + g_layout_file.toStdString());
  return g_settings_file;
}

QSettings open_settings() { return QSettings(app_settings_file_path(), QSettings::IniFormat); }

QSettings open_layout_settings() {
  return QSettings(app_layout_file_path(), QSettings::IniFormat);
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
