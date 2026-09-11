#include "webgpu_backend.h"

#include "engine/core/log.h"
#include "engine/graphics/mesh.h"
#include "engine/render/gpu_instance.h"
#include "engine/render/render_types.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#error "WebGPU RHI requires Emscripten"
#endif

#include <webgpu/webgpu.h>

namespace tamias {
namespace {

constexpr std::uint64_t kUniformSlotSize = 256;
constexpr std::uint32_t kUniformSlots = 1024;
constexpr std::uint32_t kMeshTextureSlots = kMeshTextureSetCount;

WGPUStringView wgpu_cstr(const char* s) {
  WGPUStringView v = WGPU_STRING_VIEW_INIT;
  v.data = s;
  v.length = WGPU_STRLEN;
  return v;
}

bool wait_future(WGPUInstance instance, WGPUFuture future) {
  WGPUFutureWaitInfo info = WGPU_FUTURE_WAIT_INFO_INIT;
  info.future = future;
  const WGPUWaitStatus st = wgpuInstanceWaitAny(instance, 1, &info, UINT64_MAX);
  return st == WGPUWaitStatus_Success && info.completed;
}

WGPUTextureFormat to_wgpu_format(TextureDesc::Format format) {
  switch (format) {
    case TextureDesc::Format::R8G8B8A8_UNORM:
      return WGPUTextureFormat_RGBA8Unorm;
    case TextureDesc::Format::R8G8B8A8_SRGB:
      return WGPUTextureFormat_RGBA8UnormSrgb;
    case TextureDesc::Format::B8G8R8A8_SRGB:
      return WGPUTextureFormat_BGRA8UnormSrgb;
    case TextureDesc::Format::D32_SFLOAT:
      return WGPUTextureFormat_Depth32Float;
    case TextureDesc::Format::R16G16B16A16_SFLOAT:
      return WGPUTextureFormat_RGBA16Float;
    case TextureDesc::Format::R16G16_SFLOAT:
      return WGPUTextureFormat_RG16Float;
  }
  return WGPUTextureFormat_RGBA8UnormSrgb;
}

TextureDesc::Format from_wgpu_surface_format(WGPUTextureFormat format) {
  switch (format) {
    case WGPUTextureFormat_BGRA8Unorm:
    case WGPUTextureFormat_BGRA8UnormSrgb:
      return TextureDesc::Format::B8G8R8A8_SRGB;
    case WGPUTextureFormat_RGBA8Unorm:
      return TextureDesc::Format::R8G8B8A8_UNORM;
    case WGPUTextureFormat_RGBA8UnormSrgb:
      return TextureDesc::Format::R8G8B8A8_SRGB;
    default:
      return TextureDesc::Format::B8G8R8A8_SRGB;
  }
}

class WebGpuDevice;

class WebGpuBuffer final : public Buffer {
 public:
  WebGpuBuffer(WebGpuDevice* device, WGPUBuffer buffer, BufferDesc desc)
      : device_(device), buffer_(buffer), desc_(desc) {}
  ~WebGpuBuffer() override {
    if (buffer_) {
      wgpuBufferRelease(buffer_);
    }
  }
  [[nodiscard]] const BufferDesc& desc() const override { return desc_; }
  [[nodiscard]] WGPUBuffer handle() const { return buffer_; }
  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override;

 private:
  WebGpuDevice* device_ = nullptr;
  WGPUBuffer buffer_ = nullptr;
  BufferDesc desc_{};
};

class WebGpuTexture final : public Texture {
 public:
  WebGpuTexture(WebGpuDevice* device, TextureDesc desc, WGPUTexture texture, WGPUTextureView view)
      : device_(device), desc_(desc), texture_(texture), view_(view) {}
  ~WebGpuTexture() override {
    if (view_) {
      wgpuTextureViewRelease(view_);
    }
    if (texture_) {
      wgpuTextureRelease(texture_);
    }
  }
  [[nodiscard]] const TextureDesc& desc() const override { return desc_; }
  [[nodiscard]] WGPUTextureView view() const { return view_; }
  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
    if (offset != 0) {
      return Err("WebGPU texture write offset must be 0");
    }
    return write_subresource(0, 0, data);
  }
  Result<void> write_subresource(std::uint32_t mip, std::uint32_t layer,
                                 std::span<const std::byte> data) override;

 private:
  WebGpuDevice* device_ = nullptr;
  TextureDesc desc_{};
  WGPUTexture texture_ = nullptr;
  WGPUTextureView view_ = nullptr;
};

class WebGpuShaderModule final : public ShaderModule {
 public:
  explicit WebGpuShaderModule(WGPUShaderModule module) : module_(module) {}
  ~WebGpuShaderModule() override {
    if (module_) {
      wgpuShaderModuleRelease(module_);
    }
  }
  [[nodiscard]] WGPUShaderModule handle() const { return module_; }

 private:
  WGPUShaderModule module_ = nullptr;
};

class WebGpuPipeline final : public PipelineState {
 public:
  WebGpuPipeline(WGPURenderPipeline pipeline, PrimitiveTopology topology, bool instanced)
      : pipeline_(pipeline), topology_(topology), instanced_(instanced) {}
  ~WebGpuPipeline() override {
    if (pipeline_) {
      wgpuRenderPipelineRelease(pipeline_);
    }
  }
  [[nodiscard]] WGPURenderPipeline handle() const { return pipeline_; }
  [[nodiscard]] PrimitiveTopology topology() const { return topology_; }
  [[nodiscard]] bool instanced() const { return instanced_; }

 private:
  WGPURenderPipeline pipeline_ = nullptr;
  PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;
  bool instanced_ = false;
};

class WebGpuFence final : public Fence {
 public:
  void wait() override {}
  void reset() override {}
};

class WebGpuSwapChain final : public SwapChain {
 public:
  WebGpuSwapChain(WebGpuDevice* device, std::uint32_t width, std::uint32_t height)
      : device_(device), width_(std::max(1u, width)), height_(std::max(1u, height)) {}
  ~WebGpuSwapChain() override { destroy_attachments(); }
  Result<void> resize(std::uint32_t width, std::uint32_t height) override;
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }
  [[nodiscard]] TextureDesc::Format color_format() const override;
  Result<void> configure();
  Result<void> acquire();
  void release_color();
  [[nodiscard]] WGPUTextureView color_view() const { return color_view_; }
  [[nodiscard]] WGPUTextureView depth_view() const { return depth_view_; }

 private:
  void destroy_attachments();
  Result<void> create_depth();

  WebGpuDevice* device_ = nullptr;
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
  WGPUTexture depth_ = nullptr;
  WGPUTextureView depth_view_ = nullptr;
  WGPUTexture color_tex_ = nullptr;
  WGPUTextureView color_view_ = nullptr;
};

class WebGpuCommandList final : public CommandList {
 public:
  explicit WebGpuCommandList(WebGpuDevice* device) : device_(device) {}
  ~WebGpuCommandList() override { reset(); }
  void begin() override;
  void end() override;
  void begin_render_pass(SwapChain& swap_chain, const float clear_color[4],
                         float clear_depth) override;
  void end_render_pass() override;
  void set_pipeline(PipelineState& pipeline) override;
  void set_vertex_buffer(Buffer& buffer, std::uint64_t offset = 0) override {
    vertex_ = static_cast<WebGpuBuffer*>(&buffer);
    vertex_offset_ = offset;
  }
  void set_instance_buffer(Buffer& buffer, std::uint64_t offset = 0) override {
    instance_ = static_cast<WebGpuBuffer*>(&buffer);
    instance_offset_ = offset;
  }
  void set_index_buffer(Buffer& buffer, std::uint64_t offset = 0) override {
    index_ = static_cast<WebGpuBuffer*>(&buffer);
    index_offset_ = offset;
  }
  void set_push_constants(std::span<const std::byte> data) override;
  void set_texture(Texture& texture, std::uint32_t slot = 0) override;
  void draw_indexed(const DrawIndexedDesc& desc) override;
  void set_viewport(float x, float y, float w, float h, float min_depth = 0.f,
                    float max_depth = 1.f) override;
  void set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w, std::uint32_t h) override;
  [[nodiscard]] WGPUCommandBuffer take_commands();

 private:
  void reset();
  void rebuild_bind_group();

  WebGpuDevice* device_ = nullptr;
  WGPUCommandEncoder encoder_ = nullptr;
  WGPURenderPassEncoder pass_ = nullptr;
  WGPUCommandBuffer commands_ = nullptr;
  WebGpuPipeline* pipeline_ = nullptr;
  WebGpuBuffer* vertex_ = nullptr;
  WebGpuBuffer* instance_ = nullptr;
  WebGpuBuffer* index_ = nullptr;
  std::uint64_t vertex_offset_ = 0;
  std::uint64_t instance_offset_ = 0;
  std::uint64_t index_offset_ = 0;
  std::uint32_t push_slot_ = 0;
  std::array<WebGpuTexture*, kMeshTextureSlots> textures_{};
  WGPUBindGroup bind_group_ = nullptr;
  bool bind_dirty_ = true;
};

class WebGpuDevice final : public RHIDevice {
 public:
  explicit WebGpuDevice(DeviceCreateInfo info) : info_(info) {}
  ~WebGpuDevice() override { destroy(); }

  Result<void> initialize();

  [[nodiscard]] GraphicsBackend backend() const override { return GraphicsBackend::WebGPU; }
  [[nodiscard]] Mat4 clip_space_correction_matrix() const override {
    Mat4 m = Mat4::identity();
    m(1, 1) = -1.f;
    m(2, 2) = 0.5f;
    m(2, 3) = 0.5f;
    return m;
  }

  Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc& desc) override;
  Result<std::unique_ptr<Texture>> create_texture(const TextureDesc& desc) override;
  Result<std::unique_ptr<ShaderModule>> create_shader_module(
      const ShaderModuleDesc& desc) override;
  Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc& desc) override;
  Result<std::unique_ptr<CommandList>> create_command_list() override {
    return std::make_unique<WebGpuCommandList>(this);
  }
  Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc& desc) override;
  Result<std::unique_ptr<Fence>> create_fence() override {
    return std::make_unique<WebGpuFence>();
  }

  Result<void> begin_frame(SwapChain& swap_chain) override;
  Result<void> execute(CommandList& command_list) override;
  Result<void> end_frame(SwapChain& swap_chain) override;
  void wait_idle() override {}

  [[nodiscard]] WGPUDevice device() const { return device_; }
  [[nodiscard]] WGPUQueue queue() const { return queue_; }
  [[nodiscard]] WGPUAdapter adapter() const { return adapter_; }
  [[nodiscard]] WGPUSurface surface() const { return surface_; }
  [[nodiscard]] WGPUTextureFormat surface_format() const { return surface_format_; }
  [[nodiscard]] WGPUPipelineLayout pipeline_layout() const { return pipeline_layout_; }
  [[nodiscard]] WGPUBindGroupLayout bind_group_layout() const { return bind_group_layout_; }
  [[nodiscard]] WGPUSampler sampler_for(const TextureDesc& desc) const {
    return desc.address_mode == TextureDesc::AddressMode::Clamp ? linear_clamp_ : linear_repeat_;
  }
  [[nodiscard]] WebGpuTexture* dummy_2d() const { return dummy_2d_.get(); }
  [[nodiscard]] WebGpuTexture* dummy_cube() const { return dummy_cube_.get(); }
  [[nodiscard]] WGPUBuffer uniform_buffer() const { return uniform_buffer_; }
  std::uint32_t alloc_uniform_slot(std::span<const std::byte> data);
  void flush_uniforms();
  Result<void> ensure_surface(const NativeWindowHandle& window);

 private:
  void destroy();
  Result<void> create_layout();
  Result<std::unique_ptr<WebGpuTexture>> create_solid_texture(bool cube);
  static void on_uncaptured_error(WGPUDevice const*, WGPUErrorType type, WGPUStringView message,
                                  void*, void*);

  DeviceCreateInfo info_{};
  WGPUInstance instance_ = nullptr;
  WGPUAdapter adapter_ = nullptr;
  WGPUDevice device_ = nullptr;
  WGPUQueue queue_ = nullptr;
  WGPUSurface surface_ = nullptr;
  WGPUTextureFormat surface_format_ = WGPUTextureFormat_BGRA8Unorm;
  WGPUBindGroupLayout bind_group_layout_ = nullptr;
  WGPUPipelineLayout pipeline_layout_ = nullptr;
  WGPUSampler linear_repeat_ = nullptr;
  WGPUSampler linear_clamp_ = nullptr;
  std::unique_ptr<WebGpuTexture> dummy_2d_;
  std::unique_ptr<WebGpuTexture> dummy_cube_;
  WGPUBuffer uniform_buffer_ = nullptr;
  std::vector<std::byte> uniform_cpu_;
  std::uint32_t uniform_slots_used_ = 0;
};

Result<void> WebGpuBuffer::write(std::uint64_t offset, std::span<const std::byte> data) {
  if (!desc_.host_visible) {
    return Err("buffer is not host visible");
  }
  wgpuQueueWriteBuffer(device_->queue(), buffer_, offset, data.data(), data.size());
  return {};
}

Result<void> WebGpuTexture::write_subresource(std::uint32_t mip, std::uint32_t layer,
                                              std::span<const std::byte> data) {
  const std::uint32_t mips = std::max(1u, desc_.mip_levels);
  const std::uint32_t layers = texture_layer_count(desc_);
  if (mip >= mips || layer >= layers) {
    return Err("WebGPU texture subresource out of range");
  }
  const std::uint32_t w = texture_mip_extent(desc_.width, mip);
  const std::uint32_t h = texture_mip_extent(desc_.height, mip);
  const std::size_t bpp = texture_bytes_per_pixel(desc_.format);
  const std::size_t expected = static_cast<std::size_t>(w) * h * bpp;
  if (data.size() < expected) {
    return Err("WebGPU texture write size mismatch");
  }
  const std::uint32_t unaligned = static_cast<std::uint32_t>(w * bpp);
  const std::uint32_t bytes_per_row = (unaligned + 255u) & ~255u;
  std::vector<std::byte> padded;
  const std::byte* src = data.data();
  if (bytes_per_row != unaligned) {
    padded.resize(static_cast<std::size_t>(bytes_per_row) * h);
    for (std::uint32_t row = 0; row < h; ++row) {
      std::memcpy(padded.data() + static_cast<std::size_t>(row) * bytes_per_row,
                  data.data() + static_cast<std::size_t>(row) * unaligned, unaligned);
    }
    src = padded.data();
  }
  WGPUTexelCopyTextureInfo dest = WGPU_TEXEL_COPY_TEXTURE_INFO_INIT;
  dest.texture = texture_;
  dest.mipLevel = mip;
  dest.origin.x = 0;
  dest.origin.y = 0;
  dest.origin.z = layer;
  dest.aspect = WGPUTextureAspect_All;
  WGPUTexelCopyBufferLayout layout = WGPU_TEXEL_COPY_BUFFER_LAYOUT_INIT;
  layout.bytesPerRow = bytes_per_row;
  layout.rowsPerImage = h;
  WGPUExtent3D size = WGPU_EXTENT_3D_INIT;
  size.width = w;
  size.height = h;
  size.depthOrArrayLayers = 1;
  wgpuQueueWriteTexture(device_->queue(), &dest, src,
                        bytes_per_row != unaligned ? padded.size() : expected, &layout, &size);
  return {};
}

TextureDesc::Format WebGpuSwapChain::color_format() const {
  return from_wgpu_surface_format(device_->surface_format());
}

void WebGpuSwapChain::destroy_attachments() {
  release_color();
  if (depth_view_) {
    wgpuTextureViewRelease(depth_view_);
    depth_view_ = nullptr;
  }
  if (depth_) {
    wgpuTextureRelease(depth_);
    depth_ = nullptr;
  }
}

void WebGpuSwapChain::release_color() {
  if (color_view_) {
    wgpuTextureViewRelease(color_view_);
    color_view_ = nullptr;
  }
  if (color_tex_) {
    wgpuTextureRelease(color_tex_);
    color_tex_ = nullptr;
  }
}

Result<void> WebGpuSwapChain::create_depth() {
  if (depth_view_) {
    wgpuTextureViewRelease(depth_view_);
    depth_view_ = nullptr;
  }
  if (depth_) {
    wgpuTextureRelease(depth_);
    depth_ = nullptr;
  }
  WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
  td.usage = WGPUTextureUsage_RenderAttachment;
  td.dimension = WGPUTextureDimension_2D;
  td.size = {width_, height_, 1};
  td.format = WGPUTextureFormat_Depth32Float;
  td.mipLevelCount = 1;
  td.sampleCount = 1;
  depth_ = wgpuDeviceCreateTexture(device_->device(), &td);
  if (!depth_) {
    return Err("failed to create WebGPU depth texture");
  }
  WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  vd.format = WGPUTextureFormat_Depth32Float;
  vd.dimension = WGPUTextureViewDimension_2D;
  vd.mipLevelCount = 1;
  vd.arrayLayerCount = 1;
  vd.aspect = WGPUTextureAspect_DepthOnly;
  depth_view_ = wgpuTextureCreateView(depth_, &vd);
  if (!depth_view_) {
    return Err("failed to create WebGPU depth view");
  }
  return {};
}

Result<void> WebGpuSwapChain::configure() {
  WGPUSurfaceConfiguration config = WGPU_SURFACE_CONFIGURATION_INIT;
  config.device = device_->device();
  config.format = device_->surface_format();
  config.usage = WGPUTextureUsage_RenderAttachment;
  config.width = width_;
  config.height = height_;
  config.alphaMode = WGPUCompositeAlphaMode_Auto;
  config.presentMode = WGPUPresentMode_Fifo;
  wgpuSurfaceConfigure(device_->surface(), &config);
  return create_depth();
}

Result<void> WebGpuSwapChain::resize(std::uint32_t width, std::uint32_t height) {
  width_ = std::max(1u, width);
  height_ = std::max(1u, height);
  return configure();
}

Result<void> WebGpuSwapChain::acquire() {
  release_color();
  WGPUSurfaceTexture surface_tex = WGPU_SURFACE_TEXTURE_INIT;
  wgpuSurfaceGetCurrentTexture(device_->surface(), &surface_tex);
  if (surface_tex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
      surface_tex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
    return Err("wgpuSurfaceGetCurrentTexture failed");
  }
  color_tex_ = surface_tex.texture;
  WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  vd.format = device_->surface_format();
  vd.dimension = WGPUTextureViewDimension_2D;
  vd.mipLevelCount = 1;
  vd.arrayLayerCount = 1;
  vd.aspect = WGPUTextureAspect_All;
  color_view_ = wgpuTextureCreateView(color_tex_, &vd);
  if (!color_view_) {
    return Err("failed to create WebGPU swapchain view");
  }
  return {};
}

void WebGpuCommandList::reset() {
  if (pass_) {
    wgpuRenderPassEncoderRelease(pass_);
    pass_ = nullptr;
  }
  if (encoder_) {
    wgpuCommandEncoderRelease(encoder_);
    encoder_ = nullptr;
  }
  if (commands_) {
    wgpuCommandBufferRelease(commands_);
    commands_ = nullptr;
  }
  if (bind_group_) {
    wgpuBindGroupRelease(bind_group_);
    bind_group_ = nullptr;
  }
  pipeline_ = nullptr;
  vertex_ = nullptr;
  instance_ = nullptr;
  index_ = nullptr;
  textures_ = {};
  bind_dirty_ = true;
  push_slot_ = 0;
}

void WebGpuCommandList::begin() {
  reset();
  WGPUCommandEncoderDescriptor desc = WGPU_COMMAND_ENCODER_DESCRIPTOR_INIT;
  encoder_ = wgpuDeviceCreateCommandEncoder(device_->device(), &desc);
}

void WebGpuCommandList::end() {
  if (pass_) {
    wgpuRenderPassEncoderEnd(pass_);
    wgpuRenderPassEncoderRelease(pass_);
    pass_ = nullptr;
  }
  if (encoder_) {
    WGPUCommandBufferDescriptor desc = WGPU_COMMAND_BUFFER_DESCRIPTOR_INIT;
    commands_ = wgpuCommandEncoderFinish(encoder_, &desc);
    wgpuCommandEncoderRelease(encoder_);
    encoder_ = nullptr;
  }
}

void WebGpuCommandList::begin_render_pass(SwapChain& swap_chain, const float clear_color[4],
                                          float clear_depth) {
  auto& sc = static_cast<WebGpuSwapChain&>(swap_chain);
  WGPURenderPassColorAttachment color = WGPU_RENDER_PASS_COLOR_ATTACHMENT_INIT;
  color.view = sc.color_view();
  color.loadOp = WGPULoadOp_Clear;
  color.storeOp = WGPUStoreOp_Store;
  color.clearValue = {clear_color[0], clear_color[1], clear_color[2], clear_color[3]};
  WGPURenderPassDepthStencilAttachment depth = WGPU_RENDER_PASS_DEPTH_STENCIL_ATTACHMENT_INIT;
  depth.view = sc.depth_view();
  depth.depthLoadOp = WGPULoadOp_Clear;
  depth.depthStoreOp = WGPUStoreOp_Store;
  depth.depthClearValue = clear_depth;
  depth.depthReadOnly = WGPU_FALSE;
  depth.stencilReadOnly = WGPU_TRUE;
  WGPURenderPassDescriptor rp = WGPU_RENDER_PASS_DESCRIPTOR_INIT;
  rp.colorAttachmentCount = 1;
  rp.colorAttachments = &color;
  rp.depthStencilAttachment = &depth;
  pass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &rp);
  textures_[0] = device_->dummy_2d();
  textures_[1] = device_->dummy_2d();
  textures_[2] = device_->dummy_cube();
  textures_[3] = device_->dummy_cube();
  textures_[4] = device_->dummy_2d();
  textures_[5] = device_->dummy_2d();
  bind_dirty_ = true;
}

void WebGpuCommandList::end_render_pass() {
  if (pass_) {
    wgpuRenderPassEncoderEnd(pass_);
    wgpuRenderPassEncoderRelease(pass_);
    pass_ = nullptr;
  }
}

void WebGpuCommandList::set_pipeline(PipelineState& pipeline) {
  pipeline_ = static_cast<WebGpuPipeline*>(&pipeline);
  if (pass_) {
    wgpuRenderPassEncoderSetPipeline(pass_, pipeline_->handle());
  }
}

void WebGpuCommandList::set_push_constants(std::span<const std::byte> data) {
  push_slot_ = device_->alloc_uniform_slot(data);
  bind_dirty_ = true;
}

void WebGpuCommandList::set_texture(Texture& texture, std::uint32_t slot) {
  if (slot >= kMeshTextureSlots) {
    return;
  }
  textures_[slot] = static_cast<WebGpuTexture*>(&texture);
  bind_dirty_ = true;
}

void WebGpuCommandList::rebuild_bind_group() {
  if (bind_group_) {
    wgpuBindGroupRelease(bind_group_);
    bind_group_ = nullptr;
  }
  WGPUBindGroupEntry entries[9];
  for (auto& e : entries) {
    e = WGPU_BIND_GROUP_ENTRY_INIT;
  }
  entries[0].binding = 0;
  entries[0].buffer = device_->uniform_buffer();
  entries[0].offset = 0;
  entries[0].size = kUniformSlotSize;
  entries[1].binding = 1;
  entries[1].textureView = textures_[0] ? textures_[0]->view() : device_->dummy_2d()->view();
  entries[2].binding = 2;
  entries[2].sampler = device_->sampler_for(textures_[0] ? textures_[0]->desc() : TextureDesc{});
  entries[3].binding = 3;
  entries[3].textureView = textures_[1] ? textures_[1]->view() : device_->dummy_2d()->view();
  entries[4].binding = 4;
  entries[4].textureView = textures_[2] ? textures_[2]->view() : device_->dummy_cube()->view();
  entries[5].binding = 5;
  TextureDesc cube_desc{};
  cube_desc.address_mode = TextureDesc::AddressMode::Clamp;
  entries[5].sampler = device_->sampler_for(cube_desc);
  entries[6].binding = 6;
  entries[6].textureView = textures_[3] ? textures_[3]->view() : device_->dummy_cube()->view();
  entries[7].binding = 7;
  entries[7].textureView = textures_[4] ? textures_[4]->view() : device_->dummy_2d()->view();
  entries[8].binding = 8;
  entries[8].textureView = textures_[5] ? textures_[5]->view() : device_->dummy_2d()->view();
  WGPUBindGroupDescriptor desc = WGPU_BIND_GROUP_DESCRIPTOR_INIT;
  desc.layout = device_->bind_group_layout();
  desc.entryCount = 9;
  desc.entries = entries;
  bind_group_ = wgpuDeviceCreateBindGroup(device_->device(), &desc);
  bind_dirty_ = false;
}

void WebGpuCommandList::draw_indexed(const DrawIndexedDesc& desc) {
  if (!pass_ || !pipeline_ || !vertex_ || !index_) {
    return;
  }
  if (bind_dirty_ || !bind_group_) {
    rebuild_bind_group();
  }
  if (!bind_group_) {
    return;
  }
  const std::uint32_t dyn = push_slot_ * static_cast<std::uint32_t>(kUniformSlotSize);
  wgpuRenderPassEncoderSetBindGroup(pass_, 0, bind_group_, 1, &dyn);
  wgpuRenderPassEncoderSetVertexBuffer(pass_, 0, vertex_->handle(), vertex_offset_,
                                       WGPU_WHOLE_SIZE);
  if (pipeline_->instanced() && instance_) {
    wgpuRenderPassEncoderSetVertexBuffer(pass_, 1, instance_->handle(), instance_offset_,
                                         WGPU_WHOLE_SIZE);
  }
  wgpuRenderPassEncoderSetIndexBuffer(pass_, index_->handle(), WGPUIndexFormat_Uint32,
                                      index_offset_, WGPU_WHOLE_SIZE);
  wgpuRenderPassEncoderDrawIndexed(pass_, desc.index_count, desc.instance_count, desc.first_index,
                                   desc.vertex_offset, desc.first_instance);
}

void WebGpuCommandList::set_viewport(float x, float y, float w, float h, float min_depth,
                                     float max_depth) {
  if (pass_) {
    wgpuRenderPassEncoderSetViewport(pass_, x, y, w, h, min_depth, max_depth);
  }
}

void WebGpuCommandList::set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w,
                                    std::uint32_t h) {
  if (pass_) {
    wgpuRenderPassEncoderSetScissorRect(pass_, static_cast<std::uint32_t>(std::max(0, x)),
                                        static_cast<std::uint32_t>(std::max(0, y)), w, h);
  }
}

WGPUCommandBuffer WebGpuCommandList::take_commands() {
  WGPUCommandBuffer out = commands_;
  commands_ = nullptr;
  return out;
}

std::uint32_t WebGpuDevice::alloc_uniform_slot(std::span<const std::byte> data) {
  if (uniform_slots_used_ >= kUniformSlots) {
    uniform_slots_used_ = 0;
  }
  const std::uint32_t slot = uniform_slots_used_++;
  auto* dst = uniform_cpu_.data() + static_cast<std::size_t>(slot) * kUniformSlotSize;
  std::memset(dst, 0, kUniformSlotSize);
  const std::size_t n = std::min(data.size(), static_cast<std::size_t>(kUniformSlotSize));
  std::memcpy(dst, data.data(), n);
  return slot;
}

void WebGpuDevice::flush_uniforms() {
  if (uniform_slots_used_ == 0) {
    return;
  }
  wgpuQueueWriteBuffer(queue_, uniform_buffer_, 0, uniform_cpu_.data(),
                       static_cast<std::size_t>(uniform_slots_used_) * kUniformSlotSize);
  uniform_slots_used_ = 0;
}

void WebGpuDevice::on_uncaptured_error(WGPUDevice const*, WGPUErrorType type,
                                       WGPUStringView message, void*, void*) {
  const std::string text = message.data
                               ? std::string(message.data, message.length == WGPU_STRLEN
                                                               ? std::strlen(message.data)
                                                               : message.length)
                               : std::string();
  log_error(std::string("WebGPU error ") + std::to_string(static_cast<int>(type)) + ": " + text);
}

Result<void> WebGpuDevice::ensure_surface(const NativeWindowHandle& window) {
  if (surface_) {
    return {};
  }
  const char* selectors[] = {
      window.canvas_selector && window.canvas_selector[0] ? window.canvas_selector : nullptr,
      "#viewport", "canvas"};
  for (const char* sel : selectors) {
    if (!sel) {
      continue;
    }
    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvas =
        WGPU_EMSCRIPTEN_SURFACE_SOURCE_CANVAS_HTML_SELECTOR_INIT;
    canvas.selector = wgpu_cstr(sel);
    WGPUSurfaceDescriptor surf = WGPU_SURFACE_DESCRIPTOR_INIT;
    surf.nextInChain = &canvas.chain;
    surface_ = wgpuInstanceCreateSurface(instance_, &surf);
    if (surface_) {
      WGPUSurfaceCapabilities caps = WGPU_SURFACE_CAPABILITIES_INIT;
      if (wgpuSurfaceGetCapabilities(surface_, adapter_, &caps) == WGPUStatus_Success &&
          caps.formatCount > 0) {
        surface_format_ = caps.formats[0];
        for (size_t i = 0; i < caps.formatCount; ++i) {
          if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm ||
              caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
            surface_format_ = caps.formats[i];
            break;
          }
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
      }
      log_info(std::string("WebGPU surface on ") + sel);
      return {};
    }
  }
  return Err("failed to create WebGPU canvas surface");
}

Result<void> WebGpuDevice::create_layout() {
  WGPUBindGroupLayoutEntry entries[9];
  for (auto& e : entries) {
    e = WGPU_BIND_GROUP_LAYOUT_ENTRY_INIT;
  }
  entries[0].binding = 0;
  entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
  entries[0].buffer.type = WGPUBufferBindingType_Uniform;
  entries[0].buffer.hasDynamicOffset = WGPU_TRUE;
  entries[0].buffer.minBindingSize = kUniformSlotSize;
  entries[1].binding = 1;
  entries[1].visibility = WGPUShaderStage_Fragment;
  entries[1].texture.sampleType = WGPUTextureSampleType_Float;
  entries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
  entries[2].binding = 2;
  entries[2].visibility = WGPUShaderStage_Fragment;
  entries[2].sampler.type = WGPUSamplerBindingType_Filtering;
  entries[3].binding = 3;
  entries[3].visibility = WGPUShaderStage_Fragment;
  entries[3].texture.sampleType = WGPUTextureSampleType_Float;
  entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;
  entries[4].binding = 4;
  entries[4].visibility = WGPUShaderStage_Fragment;
  entries[4].texture.sampleType = WGPUTextureSampleType_Float;
  entries[4].texture.viewDimension = WGPUTextureViewDimension_Cube;
  entries[5].binding = 5;
  entries[5].visibility = WGPUShaderStage_Fragment;
  entries[5].sampler.type = WGPUSamplerBindingType_Filtering;
  entries[6].binding = 6;
  entries[6].visibility = WGPUShaderStage_Fragment;
  entries[6].texture.sampleType = WGPUTextureSampleType_Float;
  entries[6].texture.viewDimension = WGPUTextureViewDimension_Cube;
  entries[7].binding = 7;
  entries[7].visibility = WGPUShaderStage_Fragment;
  entries[7].texture.sampleType = WGPUTextureSampleType_Float;
  entries[7].texture.viewDimension = WGPUTextureViewDimension_2D;
  entries[8].binding = 8;
  entries[8].visibility = WGPUShaderStage_Fragment;
  entries[8].texture.sampleType = WGPUTextureSampleType_Float;
  entries[8].texture.viewDimension = WGPUTextureViewDimension_2D;
  WGPUBindGroupLayoutDescriptor bgl = WGPU_BIND_GROUP_LAYOUT_DESCRIPTOR_INIT;
  bgl.entryCount = 9;
  bgl.entries = entries;
  bind_group_layout_ = wgpuDeviceCreateBindGroupLayout(device_, &bgl);
  if (!bind_group_layout_) {
    return Err("failed to create WebGPU bind group layout");
  }
  WGPUPipelineLayoutDescriptor pld = WGPU_PIPELINE_LAYOUT_DESCRIPTOR_INIT;
  pld.bindGroupLayoutCount = 1;
  pld.bindGroupLayouts = &bind_group_layout_;
  pipeline_layout_ = wgpuDeviceCreatePipelineLayout(device_, &pld);
  if (!pipeline_layout_) {
    return Err("failed to create WebGPU pipeline layout");
  }
  return {};
}

Result<std::unique_ptr<WebGpuTexture>> WebGpuDevice::create_solid_texture(bool cube) {
  TextureDesc desc{};
  desc.width = 1;
  desc.height = 1;
  desc.format = TextureDesc::Format::R8G8B8A8_UNORM;
  desc.usage = TextureDesc::Usage::Sampled;
  desc.dimension = cube ? TextureDesc::Dimension::Cube : TextureDesc::Dimension::Tex2D;
  desc.address_mode = TextureDesc::AddressMode::Clamp;
  auto tex = create_texture(desc);
  if (!tex) {
    return Err(tex.error());
  }
  const std::byte white[4] = {std::byte{255}, std::byte{255}, std::byte{255}, std::byte{255}};
  const std::uint32_t layers = cube ? 6u : 1u;
  for (std::uint32_t layer = 0; layer < layers; ++layer) {
    if (auto w = (*tex)->write_subresource(0, layer, std::as_bytes(std::span(white))); !w) {
      return Err(w.error());
    }
  }
  return std::unique_ptr<WebGpuTexture>(static_cast<WebGpuTexture*>((*tex).release()));
}

Result<void> WebGpuDevice::initialize() {
  const WGPUInstanceFeatureName timed = WGPUInstanceFeatureName_TimedWaitAny;
  WGPUInstanceDescriptor inst = WGPU_INSTANCE_DESCRIPTOR_INIT;
  inst.requiredFeatureCount = 1;
  inst.requiredFeatures = &timed;
  instance_ = wgpuCreateInstance(&inst);
  if (!instance_) {
    return Err("wgpuCreateInstance failed (TimedWaitAny required)");
  }

  struct AdapterResult {
    WGPUAdapter adapter = nullptr;
    WGPURequestAdapterStatus status = WGPURequestAdapterStatus_Error;
  } adapter_out;
  WGPURequestAdapterCallbackInfo acb = WGPU_REQUEST_ADAPTER_CALLBACK_INFO_INIT;
  acb.mode = WGPUCallbackMode_WaitAnyOnly;
  acb.userdata1 = &adapter_out;
  acb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView, void* u1,
                    void*) {
    auto* out = static_cast<AdapterResult*>(u1);
    out->status = status;
    out->adapter = adapter;
  };
  if (!wait_future(instance_, wgpuInstanceRequestAdapter(instance_, nullptr, acb)) ||
      adapter_out.status != WGPURequestAdapterStatus_Success || !adapter_out.adapter) {
    return Err("WebGPU RequestAdapter failed (browser may lack WebGPU)");
  }
  adapter_ = adapter_out.adapter;

  struct DeviceResult {
    WGPUDevice device = nullptr;
    WGPURequestDeviceStatus status = WGPURequestDeviceStatus_Error;
  } device_out;
  WGPUDeviceDescriptor dd = WGPU_DEVICE_DESCRIPTOR_INIT;
  dd.uncapturedErrorCallbackInfo.callback = on_uncaptured_error;
  WGPURequestDeviceCallbackInfo dcb = WGPU_REQUEST_DEVICE_CALLBACK_INFO_INIT;
  dcb.mode = WGPUCallbackMode_WaitAnyOnly;
  dcb.userdata1 = &device_out;
  dcb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView, void* u1,
                    void*) {
    auto* out = static_cast<DeviceResult*>(u1);
    out->status = status;
    out->device = device;
  };
  if (!wait_future(instance_, wgpuAdapterRequestDevice(adapter_, &dd, dcb)) ||
      device_out.status != WGPURequestDeviceStatus_Success || !device_out.device) {
    return Err("WebGPU RequestDevice failed");
  }
  device_ = device_out.device;
  queue_ = wgpuDeviceGetQueue(device_);

  WGPUSamplerDescriptor samp = WGPU_SAMPLER_DESCRIPTOR_INIT;
  samp.addressModeU = WGPUAddressMode_Repeat;
  samp.addressModeV = WGPUAddressMode_Repeat;
  samp.addressModeW = WGPUAddressMode_Repeat;
  samp.magFilter = WGPUFilterMode_Linear;
  samp.minFilter = WGPUFilterMode_Linear;
  samp.mipmapFilter = WGPUMipmapFilterMode_Linear;
  samp.maxAnisotropy = 1;
  linear_repeat_ = wgpuDeviceCreateSampler(device_, &samp);
  samp.addressModeU = WGPUAddressMode_ClampToEdge;
  samp.addressModeV = WGPUAddressMode_ClampToEdge;
  samp.addressModeW = WGPUAddressMode_ClampToEdge;
  linear_clamp_ = wgpuDeviceCreateSampler(device_, &samp);

  WGPUBufferDescriptor ub = WGPU_BUFFER_DESCRIPTOR_INIT;
  ub.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
  ub.size = kUniformSlotSize * kUniformSlots;
  uniform_buffer_ = wgpuDeviceCreateBuffer(device_, &ub);
  uniform_cpu_.assign(static_cast<std::size_t>(ub.size), std::byte{0});

  if (auto r = create_layout(); !r) {
    return r;
  }
  auto d2 = create_solid_texture(false);
  if (!d2) {
    return Err(d2.error());
  }
  dummy_2d_ = std::move(*d2);
  auto dc = create_solid_texture(true);
  if (!dc) {
    return Err(dc.error());
  }
  dummy_cube_ = std::move(*dc);

  NativeWindowHandle hint{};
  hint.canvas_selector = "#viewport";
  if (auto r = ensure_surface(hint); !r) {
    log_warn(r.error());
  }
  log_info("WebGPU RHI initialized");
  return {};
}

void WebGpuDevice::destroy() {
  dummy_cube_.reset();
  dummy_2d_.reset();
  if (uniform_buffer_) {
    wgpuBufferRelease(uniform_buffer_);
    uniform_buffer_ = nullptr;
  }
  if (linear_repeat_) {
    wgpuSamplerRelease(linear_repeat_);
    linear_repeat_ = nullptr;
  }
  if (linear_clamp_) {
    wgpuSamplerRelease(linear_clamp_);
    linear_clamp_ = nullptr;
  }
  if (pipeline_layout_) {
    wgpuPipelineLayoutRelease(pipeline_layout_);
    pipeline_layout_ = nullptr;
  }
  if (bind_group_layout_) {
    wgpuBindGroupLayoutRelease(bind_group_layout_);
    bind_group_layout_ = nullptr;
  }
  if (surface_) {
    wgpuSurfaceRelease(surface_);
    surface_ = nullptr;
  }
  if (queue_) {
    wgpuQueueRelease(queue_);
    queue_ = nullptr;
  }
  if (device_) {
    wgpuDeviceRelease(device_);
    device_ = nullptr;
  }
  if (adapter_) {
    wgpuAdapterRelease(adapter_);
    adapter_ = nullptr;
  }
  if (instance_) {
    wgpuInstanceRelease(instance_);
    instance_ = nullptr;
  }
}

Result<std::unique_ptr<Buffer>> WebGpuDevice::create_buffer(const BufferDesc& desc) {
  WGPUBufferDescriptor bd = WGPU_BUFFER_DESCRIPTOR_INIT;
  bd.size = std::max<std::uint64_t>(desc.size, 4);
  bd.usage = WGPUBufferUsage_CopyDst;
  if (any(desc.usage, BufferDesc::Usage::Vertex)) {
    bd.usage |= WGPUBufferUsage_Vertex;
  }
  if (any(desc.usage, BufferDesc::Usage::Index)) {
    bd.usage |= WGPUBufferUsage_Index;
  }
  if (any(desc.usage, BufferDesc::Usage::Uniform)) {
    bd.usage |= WGPUBufferUsage_Uniform;
  }
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(device_, &bd);
  if (!buffer) {
    return Err("wgpuDeviceCreateBuffer failed");
  }
  return std::make_unique<WebGpuBuffer>(this, buffer, desc);
}

Result<std::unique_ptr<Texture>> WebGpuDevice::create_texture(const TextureDesc& desc) {
  const bool cube = desc.dimension == TextureDesc::Dimension::Cube;
  WGPUTextureBindingViewDimension cube_view = WGPU_TEXTURE_BINDING_VIEW_DIMENSION_INIT;
  cube_view.textureBindingViewDimension = WGPUTextureViewDimension_Cube;
  WGPUTextureDescriptor td = WGPU_TEXTURE_DESCRIPTOR_INIT;
  td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
  td.dimension = WGPUTextureDimension_2D;
  td.size = {std::max(1u, desc.width), std::max(1u, desc.height), cube ? 6u : 1u};
  td.format = to_wgpu_format(desc.format);
  td.mipLevelCount = std::max(1u, desc.mip_levels);
  td.sampleCount = 1;
  if (cube) {
    td.nextInChain = &cube_view.chain;
  }
  WGPUTexture texture = wgpuDeviceCreateTexture(device_, &td);
  if (!texture) {
    return Err("wgpuDeviceCreateTexture failed");
  }
  WGPUTextureViewDescriptor vd = WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT;
  vd.format = td.format;
  vd.dimension = cube ? WGPUTextureViewDimension_Cube : WGPUTextureViewDimension_2D;
  vd.mipLevelCount = td.mipLevelCount;
  vd.arrayLayerCount = cube ? 6u : 1u;
  vd.aspect = WGPUTextureAspect_All;
  WGPUTextureView view = wgpuTextureCreateView(texture, &vd);
  if (!view) {
    wgpuTextureRelease(texture);
    return Err("wgpuTextureCreateView failed");
  }
  return std::make_unique<WebGpuTexture>(this, desc, texture, view);
}

Result<std::unique_ptr<ShaderModule>> WebGpuDevice::create_shader_module(
    const ShaderModuleDesc& desc) {
  if (desc.language != ShaderLanguage::Wgsl || desc.wgsl.empty()) {
    return Err("WebGPU shaders require WGSL source");
  }
  WGPUShaderSourceWGSL wgsl = WGPU_SHADER_SOURCE_WGSL_INIT;
  wgsl.code.data = desc.wgsl.data();
  wgsl.code.length = desc.wgsl.size();
  WGPUShaderModuleDescriptor sd = WGPU_SHADER_MODULE_DESCRIPTOR_INIT;
  sd.nextInChain = &wgsl.chain;
  WGPUShaderModule module = wgpuDeviceCreateShaderModule(device_, &sd);
  if (!module) {
    return Err("wgpuDeviceCreateShaderModule failed");
  }
  return std::make_unique<WebGpuShaderModule>(module);
}

Result<std::unique_ptr<PipelineState>> WebGpuDevice::create_pipeline(const PipelineDesc& desc) {
  auto* vs = static_cast<WebGpuShaderModule*>(desc.vertex_shader);
  auto* fs = static_cast<WebGpuShaderModule*>(desc.fragment_shader);
  if (!vs || !fs) {
    return Err("pipeline requires vertex and fragment shaders");
  }
  WGPUVertexAttribute attrs[4];
  for (auto& a : attrs) {
    a = WGPU_VERTEX_ATTRIBUTE_INIT;
  }
  attrs[0].format = WGPUVertexFormat_Float32x3;
  attrs[0].offset = offsetof(Vertex, position);
  attrs[0].shaderLocation = 0;
  attrs[1].format = WGPUVertexFormat_Float32x3;
  attrs[1].offset = offsetof(Vertex, normal);
  attrs[1].shaderLocation = 1;
  attrs[2].format = WGPUVertexFormat_Float32x2;
  attrs[2].offset = offsetof(Vertex, uv);
  attrs[2].shaderLocation = 2;
  attrs[3].format = WGPUVertexFormat_Float32x3;
  attrs[3].offset = offsetof(Vertex, color);
  attrs[3].shaderLocation = 3;
  WGPUVertexBufferLayout layouts[2];
  layouts[0] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  layouts[0].stepMode = WGPUVertexStepMode_Vertex;
  layouts[0].arrayStride = sizeof(Vertex);
  layouts[0].attributeCount = 4;
  layouts[0].attributes = attrs;

  WGPUVertexAttribute inst_attrs[6];
  for (auto& a : inst_attrs) {
    a = WGPU_VERTEX_ATTRIBUTE_INIT;
  }
  inst_attrs[0].format = WGPUVertexFormat_Float32x4;
  inst_attrs[0].offset = offsetof(GpuInstance, row0);
  inst_attrs[0].shaderLocation = 4;
  inst_attrs[1].format = WGPUVertexFormat_Float32x4;
  inst_attrs[1].offset = offsetof(GpuInstance, row1);
  inst_attrs[1].shaderLocation = 5;
  inst_attrs[2].format = WGPUVertexFormat_Float32x4;
  inst_attrs[2].offset = offsetof(GpuInstance, row2);
  inst_attrs[2].shaderLocation = 6;
  inst_attrs[3].format = WGPUVertexFormat_Float32x4;
  inst_attrs[3].offset = offsetof(GpuInstance, color);
  inst_attrs[3].shaderLocation = 7;
  inst_attrs[4].format = WGPUVertexFormat_Float32x4;
  inst_attrs[4].offset = offsetof(GpuInstance, material);
  inst_attrs[4].shaderLocation = 8;
  inst_attrs[5].format = WGPUVertexFormat_Float32x4;
  inst_attrs[5].offset = offsetof(GpuInstance, tex_st);
  inst_attrs[5].shaderLocation = 9;
  layouts[1] = WGPU_VERTEX_BUFFER_LAYOUT_INIT;
  layouts[1].stepMode = WGPUVertexStepMode_Instance;
  layouts[1].arrayStride = sizeof(GpuInstance);
  layouts[1].attributeCount = 6;
  layouts[1].attributes = inst_attrs;

  WGPUBlendState blend = WGPU_BLEND_STATE_INIT;
  blend.color.operation = WGPUBlendOperation_Add;
  blend.color.srcFactor = WGPUBlendFactor_One;
  blend.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
  blend.alpha.operation = WGPUBlendOperation_Add;
  blend.alpha.srcFactor = WGPUBlendFactor_One;
  blend.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
  WGPUColorTargetState color = WGPU_COLOR_TARGET_STATE_INIT;
  color.format = surface_format_;
  color.writeMask = WGPUColorWriteMask_All;
  if (desc.blend) {
    color.blend = &blend;
  }
  WGPUFragmentState frag = WGPU_FRAGMENT_STATE_INIT;
  frag.module = fs->handle();
  frag.entryPoint = wgpu_cstr("main");
  frag.targetCount = 1;
  frag.targets = &color;

  WGPUDepthStencilState depth = WGPU_DEPTH_STENCIL_STATE_INIT;
  depth.format = WGPUTextureFormat_Depth32Float;
  depth.depthWriteEnabled = desc.depth_write ? WGPUOptionalBool_True : WGPUOptionalBool_False;
  depth.depthCompare = desc.depth_test ? WGPUCompareFunction_Less : WGPUCompareFunction_Always;
  depth.stencilFront.compare = WGPUCompareFunction_Always;
  depth.stencilBack.compare = WGPUCompareFunction_Always;

  WGPURenderPipelineDescriptor pd = WGPU_RENDER_PIPELINE_DESCRIPTOR_INIT;
  pd.layout = pipeline_layout_;
  pd.vertex.module = vs->handle();
  pd.vertex.entryPoint = wgpu_cstr("main");
  pd.vertex.bufferCount = desc.instanced ? 2 : 1;
  pd.vertex.buffers = layouts;
  pd.primitive.topology = desc.topology == PrimitiveTopology::LineList
                              ? WGPUPrimitiveTopology_LineList
                              : WGPUPrimitiveTopology_TriangleList;
  pd.primitive.frontFace = WGPUFrontFace_CCW;
  pd.primitive.cullMode = WGPUCullMode_None;
  pd.depthStencil = &depth;
  pd.multisample.count = 1;
  pd.multisample.mask = 0xFFFFFFFFu;
  pd.fragment = &frag;
  WGPURenderPipeline pipeline = wgpuDeviceCreateRenderPipeline(device_, &pd);
  if (!pipeline) {
    return Err("wgpuDeviceCreateRenderPipeline failed");
  }
  return std::make_unique<WebGpuPipeline>(pipeline, desc.topology, desc.instanced);
}

Result<std::unique_ptr<SwapChain>> WebGpuDevice::create_swap_chain(const SwapChainDesc& desc) {
  if (auto r = ensure_surface(desc.window); !r) {
    return Err(r.error());
  }
  auto sc = std::make_unique<WebGpuSwapChain>(this, desc.width, desc.height);
  if (auto r = sc->configure(); !r) {
    return Err(r.error());
  }
  return sc;
}

Result<void> WebGpuDevice::begin_frame(SwapChain& swap_chain) {
  auto& sc = static_cast<WebGpuSwapChain&>(swap_chain);
  return sc.acquire();
}

Result<void> WebGpuDevice::execute(CommandList& command_list) {
  flush_uniforms();
  auto& cmds = static_cast<WebGpuCommandList&>(command_list);
  WGPUCommandBuffer buffer = cmds.take_commands();
  if (!buffer) {
    return {};
  }
  wgpuQueueSubmit(queue_, 1, &buffer);
  wgpuCommandBufferRelease(buffer);
  return {};
}

Result<void> WebGpuDevice::end_frame(SwapChain& swap_chain) {
  wgpuSurfacePresent(surface_);
  static_cast<WebGpuSwapChain&>(swap_chain).release_color();
  return {};
}

Result<std::unique_ptr<RHIDevice>> create_webgpu_device(const DeviceCreateInfo& info) {
  auto device = std::make_unique<WebGpuDevice>(info);
  if (auto r = device->initialize(); !r) {
    return Err(r.error());
  }
  return device;
}

}  // namespace

void register_webgpu_backend() {
  BackendModule module{};
  module.backend = GraphicsBackend::WebGPU;
  module.create = create_webgpu_device;
  register_backend(std::move(module));
}

}  // namespace tamias
