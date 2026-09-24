#include "engine/render/rhi/vulkan/vulkan_gpu_timing.h"

#if defined(TAMIAS_ENABLE_TRACY)

#include <cstring>
#include <new>

namespace tamias {

void VulkanGpuTiming::create(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                             VkQueue queue, VkCommandBuffer cmd) {
  if (ctx_ != nullptr) {
    return;
  }
  if (physical == VK_NULL_HANDLE || device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE ||
      cmd == VK_NULL_HANDLE) {
    return;
  }
  // 符号表模式：把 volk 的解析入口交给 Tracy，它自己取所需的入口（含
  // VK_EXT_calibrated_timestamps，拿到就把 GPU 时钟对齐到 CPU 时钟）。
  ctx_ = TracyVkContextCalibrated(instance, physical, device, queue, cmd,
                                  vkGetInstanceProcAddr, vkGetDeviceProcAddr);
}

void VulkanGpuTiming::destroy() {
  if (ctx_ == nullptr) {
    return;
  }
  while (depth_ > 0) {
    end_zone();
  }
  TracyVkDestroy(ctx_);
  ctx_ = nullptr;
}

bool VulkanGpuTiming::valid() const { return ctx_ != nullptr; }

void VulkanGpuTiming::collect(VkCommandBuffer cmd) {
  if (ctx_ == nullptr || cmd == VK_NULL_HANDLE) {
    return;
  }
  TracyVkCollect(ctx_, cmd);
}

void VulkanGpuTiming::begin_zone(VkCommandBuffer cmd, const char* name, const char* file,
                                 const char* function) {
  if (ctx_ == nullptr || cmd == VK_NULL_HANDLE || depth_ >= kMaxDepth) {
    return;
  }
  const char* zone_name = name != nullptr ? name : "gpu";
  const char* source = file != nullptr ? file : "";
  const char* fn = function != nullptr ? function : "";
  new (slots_[depth_]) tracy::VkCtxScope(ctx_, 0, source, std::strlen(source), fn,
                                        std::strlen(fn), zone_name, std::strlen(zone_name), cmd,
                                        true);
  ++depth_;
}

void VulkanGpuTiming::end_zone() {
  if (ctx_ == nullptr || depth_ == 0) {
    return;
  }
  --depth_;
  auto* scope = reinterpret_cast<tracy::VkCtxScope*>(slots_[depth_]);
  scope->~VkCtxScope();
}

}  // namespace tamias

#else

namespace tamias {

void VulkanGpuTiming::create(VkInstance, VkPhysicalDevice, VkDevice, VkQueue, VkCommandBuffer) {}

void VulkanGpuTiming::destroy() {}

bool VulkanGpuTiming::valid() const { return false; }

void VulkanGpuTiming::collect(VkCommandBuffer) {}

void VulkanGpuTiming::begin_zone(VkCommandBuffer, const char*, const char*, const char*) {}

void VulkanGpuTiming::end_zone() {}

}  // namespace tamias

#endif
