#pragma once

#include "engine/core/result.h"
#include "engine/modeling/kernel/kernel.h"

namespace tamias {

// 把编译进来的后端注册进内核注册表（对应渲染侧的 register_linked_rhi_backends）。
void register_linked_kernels();

// 进程默认内核：第一次调用时注册 linked kernels 并创建一个。
// 只为过渡 shim 存在（evaluate_feature_model 的两参重载、geometry_builder）；
// 调用方应当显式持有内核，见 docs/MODELING-KERNEL.md。
[[nodiscard]] Result<ModelKernel*> default_kernel();

}  // namespace tamias
