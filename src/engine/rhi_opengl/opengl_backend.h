#pragma once

#include "engine/rhi/device.h"

namespace tamias {

// OpenGL RHI: WGL (Win32) / GLX (X11). Never shares RenderThread/Device with other views.
void register_opengl_backend();

#if defined(_WIN32)
// UI-thread helpers: create a depth-capable child HWND for the OpenGL swapchain.
// Creating HWNDs on the render thread deadlocks with Qt's message loop.
[[nodiscard]] void* create_opengl_surface_hwnd(void* parent_hwnd, int width, int height);
void destroy_opengl_surface_hwnd(void* hwnd);
void resize_opengl_surface_hwnd(void* hwnd, int width, int height);
#endif

}  // namespace tamias
