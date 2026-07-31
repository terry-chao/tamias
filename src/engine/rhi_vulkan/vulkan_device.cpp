#include "vulkan_backend.h"

#include "core/log.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <optional>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#include <vulkan/vulkan_win32.h>
#else
#include <X11/Xlib.h>
#include <vulkan/vulkan_xlib.h>
#endif

namespace tamias {
namespace {

constexpr std::uint32_t kFramesInFlight = 2;
thread_local std::uint32_t tls_recording_frame = 0;

VKAPI_ATTR VkBool32 VKAPI_CALL debug_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data,
                                              void*) {
  if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
    log_warn(data->pMessage ? data->pMessage : "vulkan warning");
  }
  return VK_FALSE;
}

struct QueueFamilyIndices {
  std::optional<std::uint32_t> graphics;
  std::optional<std::uint32_t> present;
  [[nodiscard]] bool complete() const { return graphics && present; }
};

class VulkanBuffer final : public Buffer {
 public:
  VulkanBuffer(VmaAllocator allocator, VkBuffer buffer, VmaAllocation allocation, BufferDesc desc)
      : allocator_(allocator), buffer_(buffer), allocation_(allocation), desc_(desc) {}

  ~VulkanBuffer() override {
    if (buffer_) {
      vmaDestroyBuffer(allocator_, buffer_, allocation_);
    }
  }

  [[nodiscard]] const BufferDesc& desc() const override { return desc_; }
  [[nodiscard]] VkBuffer handle() const { return buffer_; }

  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
    if (!desc_.host_visible) {
      return Err("buffer is not host visible");
    }
    void* mapped = nullptr;
    if (vmaMapMemory(allocator_, allocation_, &mapped) != VK_SUCCESS) {
      return Err("vmaMapMemory failed");
    }
    std::memcpy(static_cast<std::byte*>(mapped) + offset, data.data(), data.size());
    vmaUnmapMemory(allocator_, allocation_);
    return {};
  }

 private:
  VmaAllocator allocator_ = nullptr;
  VkBuffer buffer_ = VK_NULL_HANDLE;
  VmaAllocation allocation_ = VK_NULL_HANDLE;
  BufferDesc desc_{};
};

class VulkanShaderModule final : public ShaderModule {
 public:
  VulkanShaderModule(VkDevice device, VkShaderModule module) : device_(device), module_(module) {}
  ~VulkanShaderModule() override {
    if (module_) {
      vkDestroyShaderModule(device_, module_, nullptr);
    }
  }
  [[nodiscard]] VkShaderModule handle() const { return module_; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkShaderModule module_ = VK_NULL_HANDLE;
};

class VulkanPipeline final : public PipelineState {
 public:
  VulkanPipeline(VkDevice device, VkPipeline pipeline, VkPipelineLayout layout)
      : device_(device), pipeline_(pipeline), layout_(layout) {}
  ~VulkanPipeline() override {
    if (pipeline_) {
      vkDestroyPipeline(device_, pipeline_, nullptr);
    }
    if (layout_) {
      vkDestroyPipelineLayout(device_, layout_, nullptr);
    }
  }
  [[nodiscard]] VkPipeline handle() const { return pipeline_; }
  [[nodiscard]] VkPipelineLayout layout() const { return layout_; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkPipeline pipeline_ = VK_NULL_HANDLE;
  VkPipelineLayout layout_ = VK_NULL_HANDLE;
};

class VulkanFence final : public Fence {
 public:
  explicit VulkanFence(VkDevice device) : device_(device) {
    VkFenceCreateInfo info{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    vkCreateFence(device_, &info, nullptr, &fence_);
  }
  ~VulkanFence() override {
    if (fence_) {
      vkDestroyFence(device_, fence_, nullptr);
    }
  }
  void wait() override { vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX); }
  void reset() override { vkResetFences(device_, 1, &fence_); }
  [[nodiscard]] VkFence handle() const { return fence_; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  VkFence fence_ = VK_NULL_HANDLE;
};

class VulkanDevice;

class VulkanSwapChain final : public SwapChain {
 public:
  VulkanSwapChain(VulkanDevice* device, SwapChainDesc desc);
  ~VulkanSwapChain() override;
  Result<void> resize(std::uint32_t width, std::uint32_t height) override;
  [[nodiscard]] std::uint32_t width() const override { return extent_.width; }
  [[nodiscard]] std::uint32_t height() const override { return extent_.height; }
  [[nodiscard]] TextureDesc::Format color_format() const override {
    return TextureDesc::Format::B8G8R8A8_SRGB;
  }

  [[nodiscard]] VkSwapchainKHR handle() const { return swapchain_; }
  [[nodiscard]] VkRenderPass render_pass() const { return render_pass_; }
  [[nodiscard]] VkFramebuffer framebuffer(std::uint32_t index) const {
    return framebuffers_[index];
  }
  [[nodiscard]] std::uint32_t image_count() const {
    return static_cast<std::uint32_t>(images_.size());
  }
  std::uint32_t& image_index() { return image_index_; }
  [[nodiscard]] std::uint32_t frame_index() const { return frame_index_; }
  void advance_frame() { frame_index_ = (frame_index_ + 1) % kFramesInFlight; }

  VkSemaphore image_available(std::uint32_t frame) const { return image_available_[frame]; }
  VkSemaphore render_finished_for_image(std::uint32_t image) const {
    return render_finished_[image];
  }
  VkFence& in_flight(std::uint32_t frame) { return in_flight_fences_[frame]; }
  const VkFence& in_flight(std::uint32_t frame) const { return in_flight_fences_[frame]; }
  VkFence& image_in_flight(std::uint32_t image) { return images_in_flight_[image]; }

 private:
  friend class VulkanDevice;
  Result<void> create_swapchain();
  void destroy_swapchain();

  VulkanDevice* device_ = nullptr;
  SwapChainDesc desc_{};
  VkSurfaceKHR surface_ = VK_NULL_HANDLE;
  VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
  VkFormat format_ = VK_FORMAT_B8G8R8A8_SRGB;
  VkExtent2D extent_{};
  std::vector<VkImage> images_;
  std::vector<VkImageView> views_;
  VkImage depth_image_ = VK_NULL_HANDLE;
  VmaAllocation depth_alloc_ = VK_NULL_HANDLE;
  VkImageView depth_view_ = VK_NULL_HANDLE;
  VkRenderPass render_pass_ = VK_NULL_HANDLE;
  std::vector<VkFramebuffer> framebuffers_;
  std::array<VkSemaphore, kFramesInFlight> image_available_{};
  std::vector<VkSemaphore> render_finished_;  // one per swapchain image
  std::array<VkFence, kFramesInFlight> in_flight_fences_{};
  std::vector<VkFence> images_in_flight_;  // fence currently using each image (non-owning)
  std::uint32_t image_index_ = 0;
  std::uint32_t frame_index_ = 0;
};

class VulkanCommandList final : public CommandList {
 public:
  VulkanCommandList(VkDevice device, VkCommandPool pool) : device_(device) {
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = pool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = kFramesInFlight;
    vkAllocateCommandBuffers(device_, &alloc, cmds_.data());
  }

  void begin() override {
    active_frame_ = tls_recording_frame;
    vkResetCommandBuffer(cmds_[active_frame_], 0);
    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmds_[active_frame_], &begin);
  }

  void end() override { vkEndCommandBuffer(cmds_[active_frame_]); }

  void begin_render_pass(SwapChain& swap_chain, const float clear_color[4],
                         float clear_depth) override {
    auto& sc = static_cast<VulkanSwapChain&>(swap_chain);
    std::array<VkClearValue, 2> clears{};
    clears[0].color = {{clear_color[0], clear_color[1], clear_color[2], clear_color[3]}};
    clears[1].depthStencil = {clear_depth, 0};
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = sc.render_pass();
    info.framebuffer = sc.framebuffer(sc.image_index());
    info.renderArea.extent = {sc.width(), sc.height()};
    info.clearValueCount = static_cast<std::uint32_t>(clears.size());
    info.pClearValues = clears.data();
    vkCmdBeginRenderPass(cmds_[active_frame_], &info, VK_SUBPASS_CONTENTS_INLINE);
    active_layout_ = VK_NULL_HANDLE;
  }

  void end_render_pass() override { vkCmdEndRenderPass(cmds_[active_frame_]); }

  void set_pipeline(PipelineState& pipeline) override {
    auto& p = static_cast<VulkanPipeline&>(pipeline);
    vkCmdBindPipeline(cmds_[active_frame_], VK_PIPELINE_BIND_POINT_GRAPHICS, p.handle());
    active_layout_ = p.layout();
  }

  void set_vertex_buffer(Buffer& buffer, std::uint64_t offset) override {
    VkBuffer buf = static_cast<VulkanBuffer&>(buffer).handle();
    vkCmdBindVertexBuffers(cmds_[active_frame_], 0, 1, &buf, &offset);
  }

  void set_index_buffer(Buffer& buffer, std::uint64_t offset) override {
    vkCmdBindIndexBuffer(cmds_[active_frame_], static_cast<VulkanBuffer&>(buffer).handle(), offset,
                         VK_INDEX_TYPE_UINT32);
  }

  void set_push_constants(std::span<const std::byte> data) override {
    if (!active_layout_) {
      return;
    }
    vkCmdPushConstants(cmds_[active_frame_], active_layout_,
                       VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0,
                       static_cast<std::uint32_t>(data.size()), data.data());
  }

  void draw_indexed(const DrawIndexedDesc& desc) override {
    vkCmdDrawIndexed(cmds_[active_frame_], desc.index_count, desc.instance_count, desc.first_index,
                     desc.vertex_offset, desc.first_instance);
  }

  void set_viewport(float x, float y, float w, float h, float min_depth, float max_depth) override {
    VkViewport vp{x, y, w, h, min_depth, max_depth};
    vkCmdSetViewport(cmds_[active_frame_], 0, 1, &vp);
  }

  void set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w, std::uint32_t h) override {
    VkRect2D sc{{x, y}, {w, h}};
    vkCmdSetScissor(cmds_[active_frame_], 0, 1, &sc);
  }

  [[nodiscard]] VkCommandBuffer handle() const { return cmds_[active_frame_]; }

 private:
  VkDevice device_ = VK_NULL_HANDLE;
  std::array<VkCommandBuffer, kFramesInFlight> cmds_{};
  std::uint32_t active_frame_ = 0;
  VkPipelineLayout active_layout_ = VK_NULL_HANDLE;
};

class VulkanDevice final : public RHIDevice {
 public:
  explicit VulkanDevice(DeviceCreateInfo info) : info_(info) {}
  ~VulkanDevice() override { destroy(); }

  Result<void> initialize();

  [[nodiscard]] GraphicsBackend backend() const override { return GraphicsBackend::Vulkan; }

  [[nodiscard]] Mat4 clip_space_correction_matrix() const override {
    // Convert OpenGL-style clip (Y up, Z -1..1) to Vulkan (Y down, Z 0..1).
    Mat4 m = Mat4::identity();
    m(1, 1) = -1.f;
    m(2, 2) = 0.5f;
    m(2, 3) = 0.5f;
    return m;
  }

  Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc& desc) override;
  Result<std::unique_ptr<Texture>> create_texture(const TextureDesc&) override {
    return Err("create_texture not used yet");
  }
  Result<std::unique_ptr<ShaderModule>> create_shader_module(const ShaderModuleDesc& desc) override;
  Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc& desc) override;
  Result<std::unique_ptr<CommandList>> create_command_list() override;
  Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc& desc) override;
  Result<std::unique_ptr<Fence>> create_fence() override {
    return std::make_unique<VulkanFence>(device_);
  }

  Result<void> begin_frame(SwapChain& swap_chain) override;
  Result<void> execute(CommandList& command_list) override;
  Result<void> end_frame(SwapChain& swap_chain) override;
  void wait_idle() override { vkDeviceWaitIdle(device_); }

  [[nodiscard]] VkDevice device() const { return device_; }
  [[nodiscard]] VkPhysicalDevice physical() const { return physical_; }
  [[nodiscard]] VmaAllocator allocator() const { return allocator_; }
  [[nodiscard]] std::uint32_t graphics_queue_family() const { return graphics_family_; }
  [[nodiscard]] VkQueue graphics_queue() const { return graphics_queue_; }
  [[nodiscard]] VkQueue present_queue() const { return present_queue_; }
  [[nodiscard]] std::uint32_t recording_frame() const { return recording_frame_; }
  [[nodiscard]] VkRenderPass shared_render_pass() const { return shared_render_pass_; }

  Result<VkSurfaceKHR> create_surface(const NativeWindowHandle& window);
  void destroy_surface(VkSurfaceKHR surface) { vkDestroySurfaceKHR(instance_, surface, nullptr); }
  QueueFamilyIndices find_queue_families(VkPhysicalDevice gpu, VkSurfaceKHR surface) const;

 private:
  friend class VulkanSwapChain;
  void destroy();
  Result<void> create_instance();
  Result<void> pick_device();
  Result<void> create_logical_device();
  Result<void> create_allocator();
  Result<void> create_command_pool();
  Result<void> create_shared_render_pass();

  DeviceCreateInfo info_{};
  VkInstance instance_ = VK_NULL_HANDLE;
  VkDebugUtilsMessengerEXT messenger_ = VK_NULL_HANDLE;
  VkPhysicalDevice physical_ = VK_NULL_HANDLE;
  VkDevice device_ = VK_NULL_HANDLE;
  VkQueue graphics_queue_ = VK_NULL_HANDLE;
  VkQueue present_queue_ = VK_NULL_HANDLE;
  std::uint32_t graphics_family_ = 0;
  std::uint32_t present_family_ = 0;
  VmaAllocator allocator_ = nullptr;
  VkCommandPool command_pool_ = VK_NULL_HANDLE;
  VkRenderPass shared_render_pass_ = VK_NULL_HANDLE;
  std::uint32_t recording_frame_ = 0;
  VulkanCommandList* pending_cmd_ = nullptr;
  VulkanSwapChain* pending_swapchain_ = nullptr;
};

VulkanSwapChain::VulkanSwapChain(VulkanDevice* device, SwapChainDesc desc)
    : device_(device), desc_(std::move(desc)) {}

VulkanSwapChain::~VulkanSwapChain() {
  destroy_swapchain();
  if (surface_) {
    device_->destroy_surface(surface_);
    surface_ = VK_NULL_HANDLE;
  }
}

Result<void> VulkanSwapChain::create_swapchain() {
  destroy_swapchain();

  if (!surface_) {
    auto surface_result = device_->create_surface(desc_.window);
    if (!surface_result) {
      return Err(surface_result.error());
    }
    surface_ = *surface_result;
  }

  VkSurfaceCapabilitiesKHR caps{};
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device_->physical(), surface_, &caps);
  std::uint32_t format_count = 0;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device_->physical(), surface_, &format_count, nullptr);
  std::vector<VkSurfaceFormatKHR> formats(format_count);
  vkGetPhysicalDeviceSurfaceFormatsKHR(device_->physical(), surface_, &format_count, formats.data());
  VkSurfaceFormatKHR chosen = formats.front();
  for (const auto& f : formats) {
    if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
        f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      chosen = f;
      break;
    }
  }
  format_ = chosen.format;

  constexpr std::uint32_t kMaxExtent = 0xffffffffu;
  if (caps.currentExtent.width != kMaxExtent) {
    extent_ = caps.currentExtent;
  } else {
    extent_.width = (std::max)(caps.minImageExtent.width,
                               (std::min)(desc_.width, caps.maxImageExtent.width));
    extent_.height = (std::max)(caps.minImageExtent.height,
                                (std::min)(desc_.height, caps.maxImageExtent.height));
  }
  if (extent_.width == 0 || extent_.height == 0) {
    return Err("swapchain extent is zero");
  }

  std::uint32_t image_count = caps.minImageCount + 1;
  if (caps.maxImageCount > 0 && image_count > caps.maxImageCount) {
    image_count = caps.maxImageCount;
  }

  const auto families = device_->find_queue_families(device_->physical(), surface_);
  const std::uint32_t qf[] = {*families.graphics, *families.present};
  VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  sci.surface = surface_;
  sci.minImageCount = image_count;
  sci.imageFormat = format_;
  sci.imageColorSpace = chosen.colorSpace;
  sci.imageExtent = extent_;
  sci.imageArrayLayers = 1;
  sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  if (*families.graphics != *families.present) {
    sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    sci.queueFamilyIndexCount = 2;
    sci.pQueueFamilyIndices = qf;
  } else {
    sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
  }
  sci.preTransform = caps.currentTransform;
  sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
  sci.presentMode = VK_PRESENT_MODE_FIFO_KHR;
  sci.clipped = VK_TRUE;
  if (vkCreateSwapchainKHR(device_->device(), &sci, nullptr, &swapchain_) != VK_SUCCESS) {
    return Err("vkCreateSwapchainKHR failed");
  }

  std::uint32_t count = 0;
  vkGetSwapchainImagesKHR(device_->device(), swapchain_, &count, nullptr);
  images_.resize(count);
  vkGetSwapchainImagesKHR(device_->device(), swapchain_, &count, images_.data());
  views_.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = images_[i];
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = format_;
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vi.subresourceRange.levelCount = 1;
    vi.subresourceRange.layerCount = 1;
    if (vkCreateImageView(device_->device(), &vi, nullptr, &views_[i]) != VK_SUCCESS) {
      return Err("vkCreateImageView failed");
    }
  }

  VkImageCreateInfo di{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
  di.imageType = VK_IMAGE_TYPE_2D;
  di.format = VK_FORMAT_D32_SFLOAT;
  di.extent = {extent_.width, extent_.height, 1};
  di.mipLevels = 1;
  di.arrayLayers = 1;
  di.samples = VK_SAMPLE_COUNT_1_BIT;
  di.tiling = VK_IMAGE_TILING_OPTIMAL;
  di.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
  VmaAllocationCreateInfo dac{};
  dac.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (vmaCreateImage(device_->allocator(), &di, &dac, &depth_image_, &depth_alloc_, nullptr) !=
      VK_SUCCESS) {
    return Err("depth image create failed");
  }
  VkImageViewCreateInfo dvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  dvi.image = depth_image_;
  dvi.viewType = VK_IMAGE_VIEW_TYPE_2D;
  dvi.format = VK_FORMAT_D32_SFLOAT;
  dvi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  dvi.subresourceRange.levelCount = 1;
  dvi.subresourceRange.layerCount = 1;
  if (vkCreateImageView(device_->device(), &dvi, nullptr, &depth_view_) != VK_SUCCESS) {
    return Err("depth view create failed");
  }

  render_pass_ = device_->shared_render_pass();
  framebuffers_.resize(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    const VkImageView attachments[] = {views_[i], depth_view_};
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = render_pass_;
    fi.attachmentCount = 2;
    fi.pAttachments = attachments;
    fi.width = extent_.width;
    fi.height = extent_.height;
    fi.layers = 1;
    if (vkCreateFramebuffer(device_->device(), &fi, nullptr, &framebuffers_[i]) != VK_SUCCESS) {
      return Err("framebuffer create failed");
    }
  }

  VkSemaphoreCreateInfo sei{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fei{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
  fei.flags = VK_FENCE_CREATE_SIGNALED_BIT;
  for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
    vkCreateSemaphore(device_->device(), &sei, nullptr, &image_available_[i]);
    vkCreateFence(device_->device(), &fei, nullptr, &in_flight_fences_[i]);
  }
  render_finished_.assign(count, VK_NULL_HANDLE);
  for (std::uint32_t i = 0; i < count; ++i) {
    vkCreateSemaphore(device_->device(), &sei, nullptr, &render_finished_[i]);
  }
  images_in_flight_.assign(count, VK_NULL_HANDLE);
  frame_index_ = 0;
  return {};
}

void VulkanSwapChain::destroy_swapchain() {
  if (!device_ || !device_->device()) {
    return;
  }
  vkDeviceWaitIdle(device_->device());
  for (auto f : in_flight_fences_) {
    if (f) {
      vkDestroyFence(device_->device(), f, nullptr);
    }
  }
  for (auto s : image_available_) {
    if (s) {
      vkDestroySemaphore(device_->device(), s, nullptr);
    }
  }
  for (auto s : render_finished_) {
    if (s) {
      vkDestroySemaphore(device_->device(), s, nullptr);
    }
  }
  image_available_ = {};
  render_finished_.clear();
  in_flight_fences_ = {};
  images_in_flight_.clear();
  for (auto fb : framebuffers_) {
    vkDestroyFramebuffer(device_->device(), fb, nullptr);
  }
  framebuffers_.clear();
  if (depth_view_) {
    vkDestroyImageView(device_->device(), depth_view_, nullptr);
    depth_view_ = VK_NULL_HANDLE;
  }
  if (depth_image_) {
    vmaDestroyImage(device_->allocator(), depth_image_, depth_alloc_);
    depth_image_ = VK_NULL_HANDLE;
    depth_alloc_ = VK_NULL_HANDLE;
  }
  for (auto v : views_) {
    vkDestroyImageView(device_->device(), v, nullptr);
  }
  views_.clear();
  images_.clear();
  if (swapchain_) {
    vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
  }
  render_pass_ = VK_NULL_HANDLE;
}

Result<void> VulkanSwapChain::resize(std::uint32_t width, std::uint32_t height) {
  desc_.width = std::max(1u, width);
  desc_.height = std::max(1u, height);
  return create_swapchain();
}

Result<void> VulkanDevice::create_instance() {
  VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
  app.pApplicationName = info_.app_name;
  app.apiVersion = VK_API_VERSION_1_2;

  auto try_create = [&](bool with_validation) -> VkResult {
    std::vector<const char*> extensions = {VK_KHR_SURFACE_EXTENSION_NAME};
#if defined(_WIN32)
    extensions.push_back(VK_KHR_WIN32_SURFACE_EXTENSION_NAME);
#else
    extensions.push_back(VK_KHR_XLIB_SURFACE_EXTENSION_NAME);
#endif
    std::vector<const char*> layers;
    VkDebugUtilsMessengerCreateInfoEXT debug_ci{
        VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pApplicationInfo = &app;
    if (with_validation) {
      layers.push_back("VK_LAYER_KHRONOS_validation");
      extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
      debug_ci.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
      debug_ci.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
      debug_ci.pfnUserCallback = debug_callback;
      ci.pNext = &debug_ci;
    }
    ci.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    ci.ppEnabledExtensionNames = extensions.data();
    ci.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    ci.ppEnabledLayerNames = layers.empty() ? nullptr : layers.data();
    const VkResult result = vkCreateInstance(&ci, nullptr, &instance_);
    if (result == VK_SUCCESS && with_validation) {
      auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
      if (create) {
        create(instance_, &debug_ci, nullptr, &messenger_);
      }
    }
    return result;
  };

  if (info_.enable_validation) {
    if (try_create(true) == VK_SUCCESS) {
      return {};
    }
    log_warn("Vulkan validation layers unavailable; continuing without them");
    info_.enable_validation = false;
  }
  if (try_create(false) != VK_SUCCESS) {
    return Err("vkCreateInstance failed");
  }
  return {};
}

QueueFamilyIndices VulkanDevice::find_queue_families(VkPhysicalDevice gpu,
                                                     VkSurfaceKHR surface) const {
  QueueFamilyIndices indices;
  std::uint32_t count = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
  std::vector<VkQueueFamilyProperties> props(count);
  vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, props.data());
  for (std::uint32_t i = 0; i < count; ++i) {
    if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphics = i;
    }
    VkBool32 present = VK_FALSE;
    if (surface) {
      vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface, &present);
    } else {
      present = VK_TRUE;
    }
    if (present) {
      indices.present = i;
    }
    if (indices.complete()) {
      break;
    }
  }
  return indices;
}

Result<void> VulkanDevice::pick_device() {
  std::uint32_t count = 0;
  vkEnumeratePhysicalDevices(instance_, &count, nullptr);
  if (count == 0) {
    return Err("no Vulkan physical devices");
  }
  std::vector<VkPhysicalDevice> devices(count);
  vkEnumeratePhysicalDevices(instance_, &count, devices.data());
  for (auto gpu : devices) {
    auto indices = find_queue_families(gpu, VK_NULL_HANDLE);
    if (!indices.graphics) {
      continue;
    }
    std::uint32_t ext_count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, nullptr);
    std::vector<VkExtensionProperties> exts(ext_count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &ext_count, exts.data());
    bool has_swapchain = false;
    for (const auto& e : exts) {
      if (std::strcmp(e.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
        has_swapchain = true;
        break;
      }
    }
    if (!has_swapchain) {
      continue;
    }
    physical_ = gpu;
    graphics_family_ = *indices.graphics;
    present_family_ = indices.present.value_or(graphics_family_);
    break;
  }
  if (!physical_) {
    return Err("no suitable Vulkan GPU");
  }
  return {};
}

Result<void> VulkanDevice::create_logical_device() {
  float priority = 1.f;
  std::vector<VkDeviceQueueCreateInfo> queues;
  std::vector<std::uint32_t> unique = {graphics_family_};
  if (present_family_ != graphics_family_) {
    unique.push_back(present_family_);
  }
  for (auto family : unique) {
    VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    q.queueFamilyIndex = family;
    q.queueCount = 1;
    q.pQueuePriorities = &priority;
    queues.push_back(q);
  }
  const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
  VkPhysicalDeviceFeatures features{};
  features.fillModeNonSolid = VK_TRUE;
  VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
  ci.queueCreateInfoCount = static_cast<std::uint32_t>(queues.size());
  ci.pQueueCreateInfos = queues.data();
  ci.enabledExtensionCount = 1;
  ci.ppEnabledExtensionNames = extensions;
  ci.pEnabledFeatures = &features;
  if (vkCreateDevice(physical_, &ci, nullptr, &device_) != VK_SUCCESS) {
    return Err("vkCreateDevice failed");
  }
  vkGetDeviceQueue(device_, graphics_family_, 0, &graphics_queue_);
  vkGetDeviceQueue(device_, present_family_, 0, &present_queue_);
  return {};
}

Result<void> VulkanDevice::create_allocator() {
  VmaAllocatorCreateInfo ci{};
  ci.physicalDevice = physical_;
  ci.device = device_;
  ci.instance = instance_;
  ci.vulkanApiVersion = VK_API_VERSION_1_2;
  if (vmaCreateAllocator(&ci, &allocator_) != VK_SUCCESS) {
    return Err("vmaCreateAllocator failed");
  }
  return {};
}

Result<void> VulkanDevice::create_command_pool() {
  VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
  ci.queueFamilyIndex = graphics_family_;
  ci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  if (vkCreateCommandPool(device_, &ci, nullptr, &command_pool_) != VK_SUCCESS) {
    return Err("vkCreateCommandPool failed");
  }
  return {};
}

Result<void> VulkanDevice::create_shared_render_pass() {
  VkAttachmentDescription color{};
  color.format = VK_FORMAT_B8G8R8A8_SRGB;
  color.samples = VK_SAMPLE_COUNT_1_BIT;
  color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentDescription depth{};
  depth.format = VK_FORMAT_D32_SFLOAT;
  depth.samples = VK_SAMPLE_COUNT_1_BIT;
  depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depth.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference color_ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference depth_ref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkSubpassDescription subpass{};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &color_ref;
  subpass.pDepthStencilAttachment = &depth_ref;

  VkSubpassDependency dep{};
  dep.srcSubpass = VK_SUBPASS_EXTERNAL;
  dep.dstSubpass = 0;
  dep.srcStageMask =
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dep.dstStageMask = dep.srcStageMask;
  dep.dstAccessMask =
      VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  const VkAttachmentDescription attachments[] = {color, depth};
  VkRenderPassCreateInfo ci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
  ci.attachmentCount = 2;
  ci.pAttachments = attachments;
  ci.subpassCount = 1;
  ci.pSubpasses = &subpass;
  ci.dependencyCount = 1;
  ci.pDependencies = &dep;
  if (vkCreateRenderPass(device_, &ci, nullptr, &shared_render_pass_) != VK_SUCCESS) {
    return Err("vkCreateRenderPass failed");
  }
  return {};
}

Result<void> VulkanDevice::initialize() {
  if (auto r = create_instance(); !r) {
    return r;
  }
  if (auto r = pick_device(); !r) {
    return r;
  }
  if (auto r = create_logical_device(); !r) {
    return r;
  }
  if (auto r = create_allocator(); !r) {
    return r;
  }
  if (auto r = create_command_pool(); !r) {
    return r;
  }
  if (auto r = create_shared_render_pass(); !r) {
    return r;
  }
  log_info("Vulkan RHI device ready");
  return {};
}

void VulkanDevice::destroy() {
  if (device_) {
    vkDeviceWaitIdle(device_);
  }
  if (shared_render_pass_) {
    vkDestroyRenderPass(device_, shared_render_pass_, nullptr);
    shared_render_pass_ = VK_NULL_HANDLE;
  }
  if (command_pool_) {
    vkDestroyCommandPool(device_, command_pool_, nullptr);
    command_pool_ = VK_NULL_HANDLE;
  }
  if (allocator_) {
    vmaDestroyAllocator(allocator_);
    allocator_ = nullptr;
  }
  if (device_) {
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
  }
  if (messenger_) {
    auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
    if (destroy) {
      destroy(instance_, messenger_, nullptr);
    }
    messenger_ = VK_NULL_HANDLE;
  }
  if (instance_) {
    vkDestroyInstance(instance_, nullptr);
    instance_ = VK_NULL_HANDLE;
  }
}

Result<VkSurfaceKHR> VulkanDevice::create_surface(const NativeWindowHandle& window) {
  VkSurfaceKHR surface = VK_NULL_HANDLE;
#if defined(_WIN32)
  VkWin32SurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
  ci.hwnd = static_cast<HWND>(window.hwnd);
  ci.hinstance = GetModuleHandleW(nullptr);
  if (vkCreateWin32SurfaceKHR(instance_, &ci, nullptr, &surface) != VK_SUCCESS) {
    return Err("vkCreateWin32SurfaceKHR failed");
  }
#else
  VkXlibSurfaceCreateInfoKHR ci{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
  ci.dpy = static_cast<Display*>(window.display);
  ci.window = static_cast<Window>(window.window);
  if (vkCreateXlibSurfaceKHR(instance_, &ci, nullptr, &surface) != VK_SUCCESS) {
    return Err("vkCreateXlibSurfaceKHR failed");
  }
#endif
  return surface;
}

Result<std::unique_ptr<Buffer>> VulkanDevice::create_buffer(const BufferDesc& desc) {
  VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
  bi.size = desc.size;
  bi.usage = 0;
  if (any(desc.usage, BufferDesc::Usage::Vertex)) {
    bi.usage |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  }
  if (any(desc.usage, BufferDesc::Usage::Index)) {
    bi.usage |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  }
  if (any(desc.usage, BufferDesc::Usage::Uniform)) {
    bi.usage |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
  }
  if (any(desc.usage, BufferDesc::Usage::TransferSrc)) {
    bi.usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  }
  if (any(desc.usage, BufferDesc::Usage::TransferDst)) {
    bi.usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  }
  VmaAllocationCreateInfo ac{};
  ac.usage = desc.host_visible ? VMA_MEMORY_USAGE_AUTO_PREFER_HOST : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
  if (desc.host_visible) {
    ac.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
               VMA_ALLOCATION_CREATE_MAPPED_BIT;
  }
  VkBuffer buffer = VK_NULL_HANDLE;
  VmaAllocation allocation = VK_NULL_HANDLE;
  if (vmaCreateBuffer(allocator_, &bi, &ac, &buffer, &allocation, nullptr) != VK_SUCCESS) {
    return Err("vmaCreateBuffer failed");
  }
  return std::make_unique<VulkanBuffer>(allocator_, buffer, allocation, desc);
}

Result<std::unique_ptr<ShaderModule>> VulkanDevice::create_shader_module(
    const ShaderModuleDesc& desc) {
  if (desc.language != ShaderLanguage::Spirv || desc.spirv.empty()) {
    return Err("Vulkan shaders require SPIR-V");
  }
  VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
  ci.codeSize = desc.spirv.size_bytes();
  ci.pCode = desc.spirv.data();
  VkShaderModule module = VK_NULL_HANDLE;
  if (vkCreateShaderModule(device_, &ci, nullptr, &module) != VK_SUCCESS) {
    return Err("vkCreateShaderModule failed");
  }
  return std::make_unique<VulkanShaderModule>(device_, module);
}

Result<std::unique_ptr<PipelineState>> VulkanDevice::create_pipeline(const PipelineDesc& desc) {
  auto* vs = static_cast<VulkanShaderModule*>(desc.vertex_shader);
  auto* fs = static_cast<VulkanShaderModule*>(desc.fragment_shader);
  if (!vs || !fs) {
    return Err("pipeline requires vertex and fragment shaders");
  }

  VkPipelineShaderStageCreateInfo stages[2]{};
  stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
  stages[0].module = vs->handle();
  stages[0].pName = "main";
  stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
  stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
  stages[1].module = fs->handle();
  stages[1].pName = "main";

  VkVertexInputBindingDescription binding{};
  binding.binding = 0;
  binding.stride = sizeof(float) * 8;  // pos3 + nrm3 + uv2
  binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  std::array<VkVertexInputAttributeDescription, 3> attrs{};
  attrs[0] = {0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0};
  attrs[1] = {1, 0, VK_FORMAT_R32G32B32_SFLOAT, sizeof(float) * 3};
  attrs[2] = {2, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 6};
  VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
  vi.vertexBindingDescriptionCount = 1;
  vi.pVertexBindingDescriptions = &binding;
  vi.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attrs.size());
  vi.pVertexAttributeDescriptions = attrs.data();

  VkPipelineInputAssemblyStateCreateInfo ia{
      VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
  ia.topology = desc.topology == PrimitiveTopology::LineList ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
                                                             : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

  VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
  vp.viewportCount = 1;
  vp.scissorCount = 1;

  VkPipelineRasterizationStateCreateInfo rs{
      VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
  rs.polygonMode = desc.wireframe ? VK_POLYGON_MODE_LINE : VK_POLYGON_MODE_FILL;
  // Do not rely on GPU face winding here: clip Y-flip + mixed asset windings make
  // frontFace easy to get wrong (hollow "see-through" solids). Solid opacity comes
  // from depth testing; mesh.frag also discards inward faces in shaded modes.
  rs.cullMode = VK_CULL_MODE_NONE;
  rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
  rs.lineWidth = 1.f;

  VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
  ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

  VkPipelineDepthStencilStateCreateInfo ds{
      VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
  ds.depthTestEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
  ds.depthWriteEnable = desc.depth_test ? VK_TRUE : VK_FALSE;
  ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
  ds.depthBoundsTestEnable = VK_FALSE;
  ds.stencilTestEnable = VK_FALSE;

  VkPipelineColorBlendAttachmentState blend_att{};
  blend_att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
  cb.attachmentCount = 1;
  cb.pAttachments = &blend_att;

  const VkDynamicState dyn_states[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
  VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
  dyn.dynamicStateCount = 2;
  dyn.pDynamicStates = dyn_states;

  VkPushConstantRange push{};
  push.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  push.offset = 0;
  push.size = 256;

  VkPipelineLayoutCreateInfo layout_ci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layout_ci.pushConstantRangeCount = 1;
  layout_ci.pPushConstantRanges = &push;
  VkPipelineLayout layout = VK_NULL_HANDLE;
  if (vkCreatePipelineLayout(device_, &layout_ci, nullptr, &layout) != VK_SUCCESS) {
    return Err("vkCreatePipelineLayout failed");
  }

  VkGraphicsPipelineCreateInfo gp{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
  gp.stageCount = 2;
  gp.pStages = stages;
  gp.pVertexInputState = &vi;
  gp.pInputAssemblyState = &ia;
  gp.pViewportState = &vp;
  gp.pRasterizationState = &rs;
  gp.pMultisampleState = &ms;
  gp.pDepthStencilState = &ds;
  gp.pColorBlendState = &cb;
  gp.pDynamicState = &dyn;
  gp.layout = layout;
  gp.renderPass = shared_render_pass_;
  VkPipeline pipeline = VK_NULL_HANDLE;
  if (vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &gp, nullptr, &pipeline) != VK_SUCCESS) {
    vkDestroyPipelineLayout(device_, layout, nullptr);
    return Err("vkCreateGraphicsPipelines failed");
  }
  return std::make_unique<VulkanPipeline>(device_, pipeline, layout);
}

Result<std::unique_ptr<CommandList>> VulkanDevice::create_command_list() {
  return std::make_unique<VulkanCommandList>(device_, command_pool_);
}

Result<std::unique_ptr<SwapChain>> VulkanDevice::create_swap_chain(const SwapChainDesc& desc) {
  auto sc = std::make_unique<VulkanSwapChain>(this, desc);
  if (auto r = sc->create_swapchain(); !r) {
    return Err(r.error());
  }
  return sc;
}

Result<void> VulkanDevice::begin_frame(SwapChain& swap_chain) {
  auto& sc = static_cast<VulkanSwapChain&>(swap_chain);
  const std::uint32_t frame = sc.frame_index();
  vkWaitForFences(device_, 1, &sc.in_flight(frame), VK_TRUE, UINT64_MAX);

  const VkResult acquired =
      vkAcquireNextImageKHR(device_, sc.handle(), UINT64_MAX, sc.image_available(frame),
                            VK_NULL_HANDLE, &sc.image_index());
  if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
    return Err("swapchain out of date");
  }
  if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
    return Err("vkAcquireNextImageKHR failed");
  }

  // If a previous frame is still using this image, wait for it.
  if (sc.image_in_flight(sc.image_index()) != VK_NULL_HANDLE) {
    vkWaitForFences(device_, 1, &sc.image_in_flight(sc.image_index()), VK_TRUE, UINT64_MAX);
  }
  sc.image_in_flight(sc.image_index()) = sc.in_flight(frame);

  vkResetFences(device_, 1, &sc.in_flight(frame));
  recording_frame_ = frame;
  tls_recording_frame = frame;
  pending_swapchain_ = &sc;
  return {};
}

Result<void> VulkanDevice::execute(CommandList& command_list) {
  pending_cmd_ = static_cast<VulkanCommandList*>(&command_list);
  return {};
}

Result<void> VulkanDevice::end_frame(SwapChain& swap_chain) {
  auto& sc = static_cast<VulkanSwapChain&>(swap_chain);
  if (!pending_cmd_ || pending_swapchain_ != &sc) {
    return Err("end_frame without matching begin/execute");
  }
  const std::uint32_t frame = sc.frame_index();
  const std::uint32_t image = sc.image_index();
  VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
  VkSemaphore wait_sem = sc.image_available(frame);
  VkSemaphore signal_sem = sc.render_finished_for_image(image);
  VkCommandBuffer cmd = pending_cmd_->handle();
  submit.waitSemaphoreCount = 1;
  submit.pWaitSemaphores = &wait_sem;
  submit.pWaitDstStageMask = &wait_stage;
  submit.commandBufferCount = 1;
  submit.pCommandBuffers = &cmd;
  submit.signalSemaphoreCount = 1;
  submit.pSignalSemaphores = &signal_sem;
  if (vkQueueSubmit(graphics_queue_, 1, &submit, sc.in_flight(frame)) != VK_SUCCESS) {
    return Err("vkQueueSubmit failed");
  }
  VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
  present.waitSemaphoreCount = 1;
  present.pWaitSemaphores = &signal_sem;
  VkSwapchainKHR swap = sc.handle();
  present.swapchainCount = 1;
  present.pSwapchains = &swap;
  present.pImageIndices = &sc.image_index();
  const VkResult presented = vkQueuePresentKHR(present_queue_, &present);
  pending_cmd_ = nullptr;
  pending_swapchain_ = nullptr;
  sc.advance_frame();
  if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
    return Err("swapchain needs resize");
  }
  if (presented != VK_SUCCESS) {
    return Err("vkQueuePresentKHR failed");
  }
  return {};
}

Result<std::unique_ptr<RHIDevice>> create_vulkan_device(const DeviceCreateInfo& info) {
  auto device = std::make_unique<VulkanDevice>(info);
  if (auto r = device->initialize(); !r) {
    return Err(r.error());
  }
  return device;
}

}  // namespace

void register_vulkan_backend() {
  register_backend(BackendModule{GraphicsBackend::Vulkan, create_vulkan_device});
}

}  // namespace tamias
