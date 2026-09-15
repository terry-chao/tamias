#include "engine/modeling/kernel/kernel.h"

#include "engine/base/log.h"

#include <mutex>
#include <unordered_map>

namespace tamias {

const char* to_string(KernelBackend backend) {
  switch (backend) {
    case KernelBackend::Occt:
      return "occt";
    case KernelBackend::Truck:
      return "truck";
  }
  return "unknown";
}

namespace {

std::mutex g_mutex;
std::unordered_map<KernelBackend, KernelModule> g_modules;

}  // namespace

void register_kernel_backend(KernelModule module) {
  std::scoped_lock lock(g_mutex);
  g_modules[module.backend] = std::move(module);
}

void clear_registered_kernel_backends() {
  std::scoped_lock lock(g_mutex);
  g_modules.clear();
}

std::vector<KernelBackend> registered_kernel_backends() {
  std::scoped_lock lock(g_mutex);
  std::vector<KernelBackend> backends;
  backends.reserve(g_modules.size());
  for (const auto& [backend, module] : g_modules) {
    (void)module;
    backends.push_back(backend);
  }
  return backends;
}

Result<std::unique_ptr<ModelKernel>> ModelKernel::create(const KernelCreateInfo& info) {
  KernelModule module;
  {
    std::scoped_lock lock(g_mutex);
    const auto it = g_modules.find(info.backend);
    if (it == g_modules.end()) {
      return Err(std::string("modeling kernel not registered: ") + to_string(info.backend));
    }
    module = it->second;
  }
  log_info(std::string("Creating modeling kernel: ") + to_string(info.backend));
  return module.create(info);
}

}  // namespace tamias
