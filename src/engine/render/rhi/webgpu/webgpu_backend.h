#pragma once

#include "engine/render/rhi/device.h"

namespace tamias {

// Browser WebGPU RHI (Emscripten + emdawnwebgpu). SwapChain binds an HTML canvas selector.
void register_webgpu_backend();

}  // namespace tamias
