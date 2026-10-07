#include "engine/render/rhi/rhi_selection_device.h"

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace tamias {

RhiSelectionDevice::RhiSelectionDevice(std::unique_ptr<RHIDevice> device)
    : device_(std::move(device)) {}

RhiSelectionDevice::~RhiSelectionDevice() = default;

RhiGpuIdentity RhiSelectionDevice::gpu_identity() const {
  return device_ != nullptr ? device_->gpu_identity() : RhiGpuIdentity{};
}

Result<void> RhiSelectionDevice::submit_noop() {
  if (device_ == nullptr) {
    return Err(std::string("submit_noop: no device"));
  }
  return device_->submit_noop();
}

// C 档：离屏画 1×1、读回、核对颜色。老实现住在 rhi_probe.cpp，现在收到这里——
// 因为「怎么离屏出图」是 RHI 的事，不是选择组件的事。
Result<void> RhiSelectionDevice::render_pixel_probe() {
  if (device_ == nullptr) {
    return Err(std::string("pixel probe: no device"));
  }
  auto target = device_->create_offscreen_swap_chain(1, 1);
  if (!target.has_value()) {
    return Err("offscreen target: " + target.error());
  }
  auto commands = device_->create_command_list();
  if (!commands.has_value()) {
    return Err("offscreen command list: " + commands.error());
  }
  const float reference[4] = {0.f, 1.f, 0.f, 1.f};  // 纯绿：读回来最好判
  if (auto began = device_->begin_frame(**target); !began) {
    return Err("offscreen begin_frame: " + began.error());
  }
  (*commands)->begin();
  (*commands)->begin_render_pass(**target, reference, 1.f);
  (*commands)->end_render_pass();
  (*commands)->end();
  if (auto executed = device_->execute(**commands); !executed) {
    return Err("offscreen execute: " + executed.error());
  }
  if (auto ended = device_->end_frame(**target); !ended) {
    return Err("offscreen end_frame: " + ended.error());
  }
  std::vector<std::uint8_t> pixels;
  if (auto read = (*target)->read_back_rgba(pixels); !read) {
    return Err("offscreen read back: " + read.error());
  }
  if (pixels.size() != 4) {
    return Err("offscreen read back returned " + std::to_string(pixels.size()) + " bytes");
  }
  if (pixels[0] > 40 || pixels[1] < 200 || pixels[2] > 40) {
    return Err(std::string("offscreen pixel check got unexpected color"));
  }
  return {};
}

void RhiSelectionDevice::wait_idle() {
  if (device_ != nullptr) {
    device_->wait_idle();
  }
}

BackendDeviceFactory make_rhi_device_factory() {
  return [](const BackendRequest& request) -> Result<std::unique_ptr<IBackendDevice>> {
    DeviceCreateInfo info{};
    info.backend = request.backend;
    info.enable_validation = request.enable_validation;
    info.app_name = request.app_name;
    auto device = RHIDevice::create(info);
    if (!device.has_value()) {
      return Err(device.error());
    }
    return std::unique_ptr<IBackendDevice>(new RhiSelectionDevice(std::move(*device)));
  };
}

}  // namespace tamias
