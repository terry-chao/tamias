#pragma once

#include "engine/render/rhi/device.h"
#include "rhi_selection/device.h"

#include <memory>

namespace tamias {

// 把 tamias 的渲染设备 RHIDevice 适配成后端选择组件认识的 IBackendDevice。
// 组件因此不需要知道 RHI 的绘制接口——它只要「身份 / 空提交 / 离屏出一个像素」。
class RhiSelectionDevice final : public IBackendDevice {
 public:
  explicit RhiSelectionDevice(std::unique_ptr<RHIDevice> device);
  ~RhiSelectionDevice() override;

  [[nodiscard]] RhiGpuIdentity gpu_identity() const override;
  [[nodiscard]] Result<void> submit_noop() override;
  [[nodiscard]] Result<void> render_pixel_probe() override;
  void wait_idle() override;

 private:
  std::unique_ptr<RHIDevice> device_;
};

// 生产用的设备工厂：按请求建 tamias 的 RHI 设备并用上面这个适配器包起来。
// 传给 rhi_selection 的 probe_rhi() / run_rhi_startup()。
[[nodiscard]] BackendDeviceFactory make_rhi_device_factory();

}  // namespace tamias
