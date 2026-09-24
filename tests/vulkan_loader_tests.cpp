#include "engine/render/rhi/device.h"

#if defined(TAMIAS_HAS_RHI_VULKAN)
#include "engine/render/rhi/vulkan/vulkan_backend.h"
#endif

#include <gtest/gtest.h>

namespace tamias {
namespace {

// volk 这条运行时加载路径的冒烟测试：能拿到 loader 并建出设备就算过。
//
// 机器上没有 Vulkan runtime（或没有可用 GPU）时**跳过**——这恰恰是 volk 的意义：
// 这种情况该是一条清晰的错误（上层据此回退 OpenGL），而不是进程加载期起不来。
TEST(VulkanLoader, CreatesDeviceWhenRuntimePresent) {
#if !defined(TAMIAS_HAS_RHI_VULKAN)
  GTEST_SKIP() << "Vulkan backend not built";
#else
  register_vulkan_backend();
  DeviceCreateInfo info{};
  info.backend = GraphicsBackend::Vulkan;
  info.enable_validation = false;
  info.app_name = "tamias-vulkan-loader-test";
  Result<std::unique_ptr<RHIDevice>> device = RHIDevice::create(info);
  if (!device.has_value()) {
    GTEST_SKIP() << "no usable Vulkan runtime: " << device.error();
  }
  EXPECT_TRUE(*device != nullptr);
#endif
}

}  // namespace
}  // namespace tamias
