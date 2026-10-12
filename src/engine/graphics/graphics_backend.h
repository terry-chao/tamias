#pragma once

// 后端枚举现在住在独立的后端选择组件里（src/rhi_selection/backend.h），
// 好让别的程序也能复用这套选择逻辑而不必拖进整个引擎。这里保留原路径做转发，
// 引擎里既有的 #include "engine/graphics/graphics_backend.h" 不用改。
#include "rhi_selection/backend.h"
