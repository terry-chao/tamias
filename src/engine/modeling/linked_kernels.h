#pragma once

#include "engine/base/result.h"
#include "engine/modeling/kernel/kernel.h"

namespace tamias {

// 把编译进来的后端注册进内核注册表（对应渲染侧的 register_linked_rhi_backends）。
void register_linked_kernels();

// 应用设置选的内核后端。必须在第一次 default_kernel() 之前调用——内核实例建好之后
// 再改就晚了（要换得重启，和渲染后端的规矩一样）。
void set_default_kernel_backend(KernelBackend backend);
[[nodiscard]] KernelBackend default_kernel_backend();

// 已注册的后端（会顺带 register_linked_kernels），给设置界面列选项用。
[[nodiscard]] std::vector<KernelBackend> available_kernels();

// 进程默认内核：第一次调用时注册 linked kernels 并创建一个。
// 只为过渡 shim 存在（evaluate_feature_model 的两参重载、geometry_builder）；
// 调用方应当显式持有内核，见 docs/MODELING-KERNEL.md。
[[nodiscard]] Result<ModelKernel*> default_kernel();

// **实际生效**的内核名字（"occt" / "truck"；一个都没有时 "none"）。
// 注意它和 default_kernel_backend() 的区别：那个是「想要哪个」，这个是「真的用上了哪个」
// ——编进来的后端里没有想要的那个时会对第一个可用的回退（见 create_with_fallback）。
// 第一次调用会触发注册与创建，所以它反映的是最终结果。
// 给 UI 显示用：桌面/服务端的默认是 occt，wasm 上取决于编进了什么。
[[nodiscard]] std::string active_kernel_name();

}  // namespace tamias
