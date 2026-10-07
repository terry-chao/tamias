#pragma once

#include "engine/base/result.h"
#include "rhi_startup/backend.h"
#include "rhi_startup/identity.h"

#include <functional>
#include <memory>
#include <string>

namespace tamias {

// Probe-time view of a backend. Any renderer (Vulkan / D3D12 / Metal / GL / Web)
// that can do these few things can use this startup-selection logic:
//   - gpu_identity()       which GPU + driver (blocklist matches on it)
//   - submit_noop()        submit one empty command and wait (validates the queue)
//   - render_pixel_probe() draw one offscreen pixel and read it back (validates output)
//   - wait_idle()          quiesce before the device is destroyed
// Defaults are "not supported"; the probe skips depth levels a backend lacks.
// This is a narrow selection-time interface, not the render API.
class IBackendDevice {
 public:
  virtual ~IBackendDevice() = default;

  [[nodiscard]] virtual RhiGpuIdentity gpu_identity() const { return {}; }
  [[nodiscard]] virtual Result<void> submit_noop() {
    return Err(std::string("submit_noop: not supported"));
  }
  [[nodiscard]] virtual Result<void> render_pixel_probe() {
    return Err(std::string("pixel probe: not supported"));
  }
  virtual void wait_idle() {}
};

// Input when creating a probe device. The host maps it onto its own device params.
struct BackendRequest {
  GraphicsBackend backend = GraphicsBackend::Vulkan;
  bool enable_validation = false;
  const char* app_name = "rhi-startup";
};

// Host-supplied: create a probe device for a backend, or a reason it failed.
// The device must be destroyed before the next candidate is tried.
using BackendDeviceFactory =
    std::function<Result<std::unique_ptr<IBackendDevice>>(const BackendRequest&)>;

}  // namespace tamias
