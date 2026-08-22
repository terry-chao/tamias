#pragma once

#include "engine/render/rhi/device.h"

namespace tamias {

// WebGL2 RHI (Emscripten / GLES 3). SwapChain binds an HTML canvas selector.
void register_webgl_backend();

}  // namespace tamias
