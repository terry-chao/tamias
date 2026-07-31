#pragma once

#include "core/native_window_handle.h"
#include "core/result.h"
#include "graphics/graphics_backend.h"
#include "math/math.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tamias {

class Buffer;
class Texture;
class ShaderModule;
class PipelineState;
class CommandList;
class SwapChain;
class Fence;

struct DeviceCreateInfo {
  GraphicsBackend backend = GraphicsBackend::Vulkan;
  bool enable_validation = true;
  const char* app_name = "tamias";
};

struct BufferDesc {
  std::uint64_t size = 0;
  enum class Usage : std::uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
    TransferSrc = 1u << 3,
    TransferDst = 1u << 4,
  };
  Usage usage = Usage::Vertex;
  bool host_visible = false;
};

inline BufferDesc::Usage operator|(BufferDesc::Usage a, BufferDesc::Usage b) {
  return static_cast<BufferDesc::Usage>(static_cast<std::uint32_t>(a) |
                                        static_cast<std::uint32_t>(b));
}
inline bool any(BufferDesc::Usage a, BufferDesc::Usage b) {
  return (static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b)) != 0;
}

struct TextureDesc {
  std::uint32_t width = 1;
  std::uint32_t height = 1;
  enum class Format { B8G8R8A8_SRGB, D32_SFLOAT };
  Format format = Format::B8G8R8A8_SRGB;
  enum class Usage : std::uint32_t {
    ColorAttachment = 1u << 0,
    DepthAttachment = 1u << 1,
    Sampled = 1u << 2,
  };
  Usage usage = Usage::ColorAttachment;
};

inline TextureDesc::Usage operator|(TextureDesc::Usage a, TextureDesc::Usage b) {
  return static_cast<TextureDesc::Usage>(static_cast<std::uint32_t>(a) |
                                         static_cast<std::uint32_t>(b));
}

struct SwapChainDesc {
  NativeWindowHandle window{};
  std::uint32_t width = 1;
  std::uint32_t height = 1;
};

enum class ShaderLanguage { Spirv, Glsl };

struct ShaderModuleDesc {
  ShaderLanguage language = ShaderLanguage::Spirv;
  std::span<const std::uint32_t> spirv;
  std::span<const char> glsl;
  std::string entry = "main";
};

enum class PrimitiveTopology { TriangleList, LineList };

struct PipelineDesc {
  ShaderModule* vertex_shader = nullptr;
  ShaderModule* fragment_shader = nullptr;
  PrimitiveTopology topology = PrimitiveTopology::TriangleList;
  bool depth_test = true;
  bool wireframe = false;
  TextureDesc::Format color_format = TextureDesc::Format::B8G8R8A8_SRGB;
  TextureDesc::Format depth_format = TextureDesc::Format::D32_SFLOAT;
};

struct DrawIndexedDesc {
  std::uint32_t index_count = 0;
  std::uint32_t instance_count = 1;
  std::uint32_t first_index = 0;
  std::int32_t vertex_offset = 0;
  std::uint32_t first_instance = 0;
};

class Buffer {
 public:
  virtual ~Buffer() = default;
  [[nodiscard]] virtual const BufferDesc& desc() const = 0;
  virtual Result<void> write(std::uint64_t offset, std::span<const std::byte> data) = 0;
};

class Texture {
 public:
  virtual ~Texture() = default;
  [[nodiscard]] virtual const TextureDesc& desc() const = 0;
};

class ShaderModule {
 public:
  virtual ~ShaderModule() = default;
};

class PipelineState {
 public:
  virtual ~PipelineState() = default;
};

class Fence {
 public:
  virtual ~Fence() = default;
  virtual void wait() = 0;
  virtual void reset() = 0;
};

class SwapChain {
 public:
  virtual ~SwapChain() = default;
  virtual Result<void> resize(std::uint32_t width, std::uint32_t height) = 0;
  [[nodiscard]] virtual std::uint32_t width() const = 0;
  [[nodiscard]] virtual std::uint32_t height() const = 0;
  [[nodiscard]] virtual TextureDesc::Format color_format() const = 0;
};

class CommandList {
 public:
  virtual ~CommandList() = default;
  virtual void begin() = 0;
  virtual void end() = 0;
  virtual void begin_render_pass(SwapChain& swap_chain, const float clear_color[4],
                                 float clear_depth) = 0;
  virtual void end_render_pass() = 0;
  virtual void set_pipeline(PipelineState& pipeline) = 0;
  virtual void set_vertex_buffer(Buffer& buffer, std::uint64_t offset = 0) = 0;
  virtual void set_index_buffer(Buffer& buffer, std::uint64_t offset = 0) = 0;
  virtual void set_push_constants(std::span<const std::byte> data) = 0;
  virtual void draw_indexed(const DrawIndexedDesc& desc) = 0;
  virtual void set_viewport(float x, float y, float w, float h, float min_depth = 0.f,
                            float max_depth = 1.f) = 0;
  virtual void set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w,
                           std::uint32_t h) = 0;
};

class RHIDevice {
 public:
  virtual ~RHIDevice() = default;

  [[nodiscard]] virtual GraphicsBackend backend() const = 0;
  [[nodiscard]] virtual Mat4 clip_space_correction_matrix() const = 0;

  virtual Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc& desc) = 0;
  virtual Result<std::unique_ptr<Texture>> create_texture(const TextureDesc& desc) = 0;
  virtual Result<std::unique_ptr<ShaderModule>> create_shader_module(
      const ShaderModuleDesc& desc) = 0;
  virtual Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc& desc) = 0;
  virtual Result<std::unique_ptr<CommandList>> create_command_list() = 0;
  virtual Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc& desc) = 0;
  virtual Result<std::unique_ptr<Fence>> create_fence() = 0;

  virtual Result<void> begin_frame(SwapChain& swap_chain) = 0;
  virtual Result<void> execute(CommandList& command_list) = 0;
  virtual Result<void> end_frame(SwapChain& swap_chain) = 0;
  virtual void wait_idle() = 0;

  static Result<std::unique_ptr<RHIDevice>> create(const DeviceCreateInfo& info);
};

struct BackendModule {
  GraphicsBackend backend{};
  std::function<Result<std::unique_ptr<RHIDevice>>(const DeviceCreateInfo&)> create;
};

void register_backend(BackendModule module);
void clear_registered_backends();

}  // namespace tamias
