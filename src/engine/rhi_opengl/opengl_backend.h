#pragma once

#include "engine/rhi/device.h"

namespace tamias {

// M4: OpenGL backend registration stub. Real WGL/GLX isolation comes later.
// OpenGL must never share RenderThread/Device with other views.
void register_opengl_backend();

}  // namespace tamias
