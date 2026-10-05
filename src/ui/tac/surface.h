#pragma once

#include "engine/base/native_window_handle.h"
#include "ui/tac/core/input.h"
#include "ui/tac/core/types.h"

namespace tac {

struct SurfaceDesc {
  Size size{};
  float dpr = 1.0F;
  bool offscreen = false;
};

// 上层（视口控制器）接收归一化后的输入事件。默认实现为空，用哪个重写哪个。
class SurfaceEventSink {
 public:
  virtual ~SurfaceEventSink() = default;

  virtual void on_mouse(const MouseEvent& /*event*/) {}
  virtual void on_wheel(const WheelEvent& /*event*/) {}
  virtual void on_key(const KeyEvent& /*event*/) {}
  virtual void on_resize(const ResizeEvent& /*event*/) {}
  virtual void on_destroy() {}
};

// 一块可渲染的画布。Qt 后端用 QWidget + QRhi 实现；自研后端只要拿得出
// NativeWindowHandle 就能直接喂给 RenderChannel（渲染层已经与 Qt 解耦）。
class ViewportSurface {
 public:
  virtual ~ViewportSurface() = default;

  [[nodiscard]] virtual tamias::NativeWindowHandle native_handle() const = 0;
  [[nodiscard]] virtual Size pixel_size() const = 0;
  [[nodiscard]] virtual float dpr() const = 0;

  virtual void set_event_sink(SurfaceEventSink* sink) = 0;
  virtual void request_redraw() = 0;
};

}  // namespace tac
