#pragma once

#include "engine/base/native_window_handle.h"
#include "ui/tac/core/types.h"

#include <string>
#include <string_view>

namespace tac {

struct WindowDesc {
  std::string title;
  Size size{1280.0F, 720.0F};
  bool resizable = true;
  bool maximized = false;
};

// 顶层窗口。渲染表面（ViewportSurface）由后端另外创建并挂进窗口；
// 事件循环与帧节奏留在后端，业务层不驱动帧（沿用 Session 的约定）。
class Window {
 public:
  virtual ~Window() = default;

  virtual void show() = 0;
  virtual void hide() = 0;
  virtual void close() = 0;
  virtual void set_title(std::string_view title) = 0;

  [[nodiscard]] virtual Size size() const = 0;
  // 交给渲染线程的裸句柄，见 engine/base/native_window_handle.h。
  [[nodiscard]] virtual tamias::NativeWindowHandle native_handle() const = 0;
};

}  // namespace tac
