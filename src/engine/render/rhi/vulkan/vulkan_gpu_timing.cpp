#include "engine/render/rhi/vulkan/vulkan_gpu_timing.h"

#if defined(TAMIAS_ENABLE_TRACY)

#include <cstring>
#include <new>

namespace tamias {

void VulkanGpuTiming::create(VkPhysicalDevice physical, VkDevice device, VkQueue queue,
                             VkCommandBuffer cmd,
                             PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT time_domains,
                             PFN_vkGetCalibratedTimestampsEXT calibrated) {
  if (ctx_ != nullptr) {
    return;
  }
  if (physical == VK_NULL_HANDLE || device == VK_NULL_HANDLE || queue == VK_NULL_HANDLE ||
      cmd == VK_NULL_HANDLE) {
    return;
  }
  // 有 VK_EXT_calibrated_timestamps 就把函数指针交给 Tracy，它会把 GPU 时钟对齐到
  // CPU 时钟；没有就退回 DEVICE 时间域。
  if (time_domains != nullptr && calibrated != nullptr) {
    ctx_ = TracyVkContextCalibrated(physical, device, queue, cmd, time_domains, calibrated);
  } else {
    ctx_ = TracyVkContext(physical, device, queue, cmd);
  }
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

void VulkanGpuTiming::create(VkPhysicalDevice, VkDevice, VkQueue, VkCommandBuffer,
                             PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
                             PFN_vkGetCalibratedTimestampsEXT) {}

void VulkanGpuTiming::destroy() {}

bool VulkanGpuTiming::valid() const { return false; }

void VulkanGpuTiming::collect(VkCommandBuffer) {}

void VulkanGpuTiming::begin_zone(VkCommandBuffer, const char*, const char*, const char*) {}

void VulkanGpuTiming::end_zone() {}

}  // namespace tamias

#endif
