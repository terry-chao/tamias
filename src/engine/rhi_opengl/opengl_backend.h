#pragma once

#include "engine/rhi/device.h"

namespace tamias {

// OpenGL RHI: WGL (Win32) / GLX (X11). Never shares RenderThread/Device with other views.
void register_opengl_backend();

}  // namespace tamias
