#pragma once

#include "engine/render/runtime/render_runtime.h"
#include "engine/graphics/graphics_backend.h"

#include <QString>
#include <QStringList>
#include <QByteArray>

namespace tamias {

enum class UiColorScheme { System, Light, Dark };

class AppSettings {
 public:
  static AppSettings& instance();

  void load();
  void save() const;

  [[nodiscard]] GraphicsBackend graphics_backend() const { return graphics_backend_; }
  void set_graphics_backend(GraphicsBackend backend);

  // 建模内核后端（"occt" / "truck"）。只影响参数化求值走哪个内核，不影响文件格式。
  [[nodiscard]] QString kernel_backend() const { return kernel_backend_; }
  void set_kernel_backend(const QString& backend);

  [[nodiscard]] QString ui_language() const { return ui_language_; }
  void set_ui_language(const QString& language);

  [[nodiscard]] UiColorScheme ui_color_scheme() const { return ui_color_scheme_; }
  void set_ui_color_scheme(UiColorScheme scheme);

  [[nodiscard]] bool zoom_to_mouse_position() const { return zoom_to_mouse_position_; }
  void set_zoom_to_mouse_position(bool enabled);

  // Ribbon 形态："text"（图标 + 文字）或 "icons"（仅图标，悬浮出提示）。
  [[nodiscard]] QString ribbon_style() const { return ribbon_style_; }
  void set_ribbon_style(const QString& style);
  // 被拖出 Ribbon、漂在外面的分组："page_id|group_id|x|y"。
  [[nodiscard]] QStringList ribbon_floating_groups() const {
    return ribbon_floating_groups_;
  }
  void set_ribbon_floating_groups(const QStringList& entries);
  // 整条 Ribbon 的分组布局："page_id|group_id|row|index"（见 RibbonBar::layout_keys）。
  [[nodiscard]] QStringList ribbon_layout() const { return ribbon_layout_; }
  void set_ribbon_layout(const QStringList& entries);
  // Ribbon 卷起（只留页签那一条）。
  [[nodiscard]] bool ribbon_collapsed() const { return ribbon_collapsed_; }
  void set_ribbon_collapsed(bool collapsed);
  // 主窗口的面板停靠布局（QMainWindow::saveState：每个面板停在哪一区、多大、
  // 是否浮动、显不显示）。拖动面板之后记下来，下次开还是这样。
  [[nodiscard]] QByteArray window_state() const { return window_state_; }
  void set_window_state(const QByteArray& state);

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

  // ==== 启动探测的结果（见 docs/RHI-STARTUP.md）====
  // 用户偏好（graphics_backend_）是「想用哪个」；这里是「本次实际用哪个」。
  // 探测降级只覆盖后者，不写回用户偏好——否则驱动修好了也回不到 Vulkan。
  [[nodiscard]] GraphicsBackend resolved_backend() const;
  void set_resolved_backend(GraphicsBackend backend) { resolved_backend_ = backend; }
  [[nodiscard]] bool resolved_backend_set() const { return resolved_backend_.has_value(); }
  [[nodiscard]] bool safe_mode() const { return safe_mode_; }
  void set_safe_mode(bool on) { safe_mode_ = on; }
  [[nodiscard]] bool backend_locked() const { return backend_locked_; }
  void set_backend_locked(bool locked) { backend_locked_ = locked; }
  // 上次跑通的后端 + 当时的 GPU 指纹（换显卡 / 升驱动后指纹变，就不再直接信任它）。
  [[nodiscard]] std::optional<GraphicsBackend> last_good_backend() const {
    return last_good_backend_;
  }
  [[nodiscard]] QString last_good_gpu_fingerprint() const { return last_good_gpu_fingerprint_; }
  void set_last_good_backend(GraphicsBackend backend, const QString& fingerprint);

 private:
  AppSettings() = default;

  GraphicsBackend graphics_backend_ = GraphicsBackend::Vulkan;
  QString kernel_backend_ = QStringLiteral("occt");
  QString ui_language_ = QStringLiteral("system");
  UiColorScheme ui_color_scheme_ = UiColorScheme::System;
  bool zoom_to_mouse_position_ = true;
  QString ribbon_style_ = QStringLiteral("text");
  QStringList ribbon_floating_groups_;
  QStringList ribbon_layout_;
  bool ribbon_collapsed_ = false;
  QByteArray window_state_;
  QStringList disabled_plugin_ids_;
  QStringList ribbon_command_order_;
  std::optional<GraphicsBackend> resolved_backend_;
  std::optional<GraphicsBackend> last_good_backend_;
  QString last_good_gpu_fingerprint_;
  bool safe_mode_ = false;
  bool backend_locked_ = false;
};

void apply_ui_color_scheme(UiColorScheme scheme);

}  // namespace tamias
