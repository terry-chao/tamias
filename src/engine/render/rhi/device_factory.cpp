#include "device.h"

#include "engine/core/log.h"

#include <mutex>
#include <unordered_map>

namespace tamias {
namespace {

std::mutex g_mutex;
std::unordered_map<GraphicsBackend, BackendModule> g_backends;

}  // namespace

void register_backend(BackendModule module) {
  std::scoped_lock lock(g_mutex);
  g_backends[module.backend] = std::move(module);
}

void clear_registered_backends() {
  std::scoped_lock lock(g_mutex);
  g_backends.clear();
}

Result<std::unique_ptr<RHIDevice>> RHIDevice::create(const DeviceCreateInfo& info) {
  BackendModule module;
  {
    std::scoped_lock lock(g_mutex);
    const auto it = g_backends.find(info.backend);
    if (it == g_backends.end()) {
      return Err(std::string("RHI backend not registered: ") + to_string(info.backend));
    }
    module = it->second;
  }
  log_info(std::string("Creating RHI device: ") + to_string(info.backend));
  return module.create(info);
}

}  // namespace tamias
