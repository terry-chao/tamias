#pragma once

#include <cstddef>

#include <vulkan/vulkan.h>

#if defined(TAMIAS_ENABLE_TRACY)
#include <tracy/TracyVulkan.hpp>
#endif

namespace tamias {

// Tracy 的 Vulkan GPU 计时上下文：用 vkCmdWriteTimestamp 量 GPU 侧耗时，
// 结果作为 GPU 行出现在 Tracy 时间线上（和 CPU zone 并排）。
//
// 生命周期跟着一个 command list 走（本后端一个 command list = 一个图形队列）。
// 没编 Tracy 时所有方法都是空实现，调用点不需要 #ifdef。
class VulkanGpuTiming {
 public:
  VulkanGpuTiming() = default;
  ~VulkanGpuTiming() { destroy(); }

  VulkanGpuTiming(const VulkanGpuTiming&) = delete;
  VulkanGpuTiming& operator=(const VulkanGpuTiming&) = delete;

  // cmd 必须是没在录制状态的 command buffer：Tracy 会拿它做一次初始化提交并
  // vkQueueWaitIdle（所以只在刚建好 command list 时调用）。两个扩展函数指针
  // 可以为空 —— 为空时 Tracy 退回 DEVICE 时间域，区间长度仍然准，只是绝对位置会漂。
  void create(VkPhysicalDevice physical, VkDevice device, VkQueue queue, VkCommandBuffer cmd,
              PFN_vkGetPhysicalDeviceCalibrateableTimeDomainsEXT time_domains,
              PFN_vkGetCalibratedTimestampsEXT calibrated);
  void destroy();

  [[nodiscard]] bool valid() const;

  // 每帧收一次：把已完成的 timestamp 读回来发给 profiler。必须在录制中、且在
  // render pass 之外调用（Tracy 内部要用 vkCmdResetQueryPool）。
  void collect(VkCommandBuffer cmd);

  // 嵌套 GPU zone，必须与 end_zone() 配对。名字会被 Tracy 拷贝，运行时字符串安全。
  void begin_zone(VkCommandBuffer cmd, const char* name, const char* file, const char* function);
  void end_zone();

 private:
#if defined(TAMIAS_ENABLE_TRACY)
  static constexpr std::size_t kMaxDepth = 8;

  tracy::VkCtx* ctx_ = nullptr;
  std::size_t depth_ = 0;
  // VkCtxScope 不可移动，用定长槽位 + placement new 当栈使。
  alignas(tracy::VkCtxScope) unsigned char slots_[kMaxDepth][sizeof(tracy::VkCtxScope)]{};
#endif
};

}  // namespace tamias
