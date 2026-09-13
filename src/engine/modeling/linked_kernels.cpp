#include "engine/modeling/linked_kernels.h"

#include <mutex>
#include <string>

#if defined(TAMIAS_HAS_KERNEL_OCCT)
#include "engine/modeling/occt/occt_kernel.h"
#endif

#if defined(TAMIAS_HAS_KERNEL_TRUCK)
#include "engine/modeling/truck/truck_kernel.h"
#endif

namespace tamias {
namespace {

std::once_flag g_once;
std::unique_ptr<ModelKernel> g_default;
std::string g_error;
KernelBackend g_backend = KernelBackend::Occt;

}  // namespace

void register_linked_kernels() {
#if defined(TAMIAS_HAS_KERNEL_OCCT)
  register_occt_kernel_backend();
#endif
#if defined(TAMIAS_HAS_KERNEL_TRUCK)
  register_truck_kernel_backend();
#endif
}

void set_default_kernel_backend(KernelBackend backend) { g_backend = backend; }

KernelBackend default_kernel_backend() { return g_backend; }

std::vector<KernelBackend> available_kernels() {
  register_linked_kernels();
  return registered_kernel_backends();
}

Result<ModelKernel*> default_kernel() {
  std::call_once(g_once, [] {
    register_linked_kernels();
    auto created = ModelKernel::create(KernelCreateInfo{g_backend});
    if (created) {
      g_default = std::move(*created);
    } else {
      g_error = created.error();
    }
  });
  if (g_default == nullptr) {
    return Err(g_error.empty() ? std::string("no modeling kernel available") : g_error);
  }
  return g_default.get();
}

}  // namespace tamias
