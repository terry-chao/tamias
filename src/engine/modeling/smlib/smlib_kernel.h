#pragma once

namespace tamias {

// 把 SMLib（NVIDIA USD BRep 的 SMLib + SM_API）注册成建模内核后端。
// 只在 TAMIAS_HAS_KERNEL_SMLIB 的构建里被 linked_kernels.cpp 调用。
void register_smlib_kernel_backend();

}  // namespace tamias
