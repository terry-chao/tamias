#include "webgl_backend.h"

#include "engine/core/log.h"
#include "engine/graphics/mesh.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#if !defined(__EMSCRIPTEN__)
#error "WebGL RHI requires Emscripten"
#endif

#include <emscripten/html5.h>
#include <GLES3/gl3.h>

namespace tamias {
namespace {

constexpr GLuint kPushConstantBinding = 0;

class WebGLDevice;

class WebGLBuffer final : public Buffer {
 public:
  WebGLBuffer(GLuint buffer, BufferDesc desc, GLenum target)
      : buffer_(buffer), desc_(desc), target_(target) {}
  ~WebGLBuffer() override {
    if (buffer_) {
      glDeleteBuffers(1, &buffer_);
    }
  }
  [[nodiscard]] const BufferDesc& desc() const override { return desc_; }
  [[nodiscard]] GLuint handle() const { return buffer_; }
  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
    if (!desc_.host_visible) {
      return Err("buffer is not host visible");
    }
    glBindBuffer(target_, buffer_);
    glBufferSubData(target_, static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(data.size()),
                    data.data());
    return {};
  }

 private:
  GLuint buffer_ = 0;
  BufferDesc desc_{};
  GLenum target_ = GL_ARRAY_BUFFER;
};

class WebGLTexture final : public Texture {
 public:
  WebGLTexture(TextureDesc desc, GLuint texture) : desc_(desc), texture_(texture) {}
  ~WebGLTexture() override {
    if (texture_) {
      glDeleteTextures(1, &texture_);
    }
  }
  [[nodiscard]] const TextureDesc& desc() const override { return desc_; }
  [[nodiscard]] GLuint handle() const { return texture_; }
  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
    if (offset != 0) {
      return Err("WebGL texture write offset must be 0");
    }
    const std::size_t expected =
        static_cast<std::size_t>(desc_.width) * desc_.height * 4;
    if (data.size() != expected) {
      return Err("WebGL texture write size mismatch");
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(desc_.width),
                    static_cast<GLsizei>(desc_.height), GL_RGBA, GL_UNSIGNED_BYTE, data.data());
    return {};
  }

 private:
  TextureDesc desc_{};
  GLuint texture_ = 0;
};

class WebGLShaderModule final : public ShaderModule {
 public:
  explicit WebGLShaderModule(GLuint shader) : shader_(shader) {}
  ~WebGLShaderModule() override {
    if (shader_) {
      glDeleteShader(shader_);
    }
  }
  [[nodiscard]] GLuint handle() const { return shader_; }

 private:
  GLuint shader_ = 0;
};

class WebGLPipeline final : public PipelineState {
 public:
  WebGLPipeline(GLuint program, bool depth_test, bool depth_write, PrimitiveTopology topology)
      : program_(program),
        depth_test_(depth_test),
        depth_write_(depth_write),
        topology_(topology) {}
  ~WebGLPipeline() override {
    if (program_) {
      glDeleteProgram(program_);
    }
  }
  [[nodiscard]] GLuint program() const { return program_; }
  [[nodiscard]] bool depth_test() const { return depth_test_; }
  [[nodiscard]] bool depth_write() const { return depth_write_; }
  [[nodiscard]] PrimitiveTopology topology() const { return topology_; }

 private:
  GLuint program_ = 0;
  bool depth_test_ = true;
  bool depth_write_ = true;
  PrimitiveTopology topology_ = PrimitiveTopology::TriangleList;
};

class WebGLFence final : public Fence {
 public:
  void wait() override { glFinish(); }
  void reset() override {}
};

class WebGLSwapChain final : public SwapChain {
 public:
  WebGLSwapChain(SwapChainDesc desc) : window_(desc.window), width_(std::max(1u, desc.width)),
                                       height_(std::max(1u, desc.height)) {}
  Result<void> resize(std::uint32_t width, std::uint32_t height) override {
    width_ = std::max(1u, width);
    height_ = std::max(1u, height);
    return {};
  }
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }
  [[nodiscard]] TextureDesc::Format color_format() const override {
    return TextureDesc::Format::R8G8B8A8_SRGB;
  }

 private:
  NativeWindowHandle window_{};
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
};

class WebGLCommandList final : public CommandList {
 public:
  explicit WebGLCommandList(WebGLDevice* device) : device_(device) {}
  void begin() override {}
  void end() override {}
  void begin_render_pass(SwapChain&, const float clear_color[4], float clear_depth) override;
  void end_render_pass() override {}
  void set_pipeline(PipelineState& pipeline) override;
  void set_vertex_buffer(Buffer& buffer, std::uint64_t offset = 0) override {
    vertex_ = static_cast<WebGLBuffer*>(&buffer);
    vertex_offset_ = offset;
  }
  void set_index_buffer(Buffer& buffer, std::uint64_t offset = 0) override {
    index_ = static_cast<WebGLBuffer*>(&buffer);
    index_offset_ = offset;
  }
  void set_push_constants(std::span<const std::byte> data) override;
  void set_texture(Texture& texture, std::uint32_t slot = 0) override {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, static_cast<WebGLTexture&>(texture).handle());
  }
  void draw_indexed(const DrawIndexedDesc& desc) override;
  void set_viewport(float x, float y, float w, float h, float, float) override {
    glViewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w),
               static_cast<GLsizei>(h));
  }
  void set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w, std::uint32_t h) override {
    glScissor(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
  }

 private:
  WebGLDevice* device_ = nullptr;
  WebGLPipeline* pipeline_ = nullptr;
  WebGLBuffer* vertex_ = nullptr;
  WebGLBuffer* index_ = nullptr;
  std::uint64_t vertex_offset_ = 0;
  std::uint64_t index_offset_ = 0;
};

class WebGLDevice final : public RHIDevice {
 public:
  explicit WebGLDevice(DeviceCreateInfo info) : info_(info) {}
  ~WebGLDevice() override { destroy(); }

  Result<void> initialize();

  [[nodiscard]] GraphicsBackend backend() const override { return GraphicsBackend::WebGL; }
  [[nodiscard]] Mat4 clip_space_correction_matrix() const override {
    Mat4 m = Mat4::identity();
    m(2, 2) = 2.f;
    m(2, 3) = -1.f;
    return m;
  }

  Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc& desc) override;
  Result<std::unique_ptr<Texture>> create_texture(const TextureDesc& desc) override;
  Result<std::unique_ptr<ShaderModule>> create_shader_module(
      const ShaderModuleDesc& desc) override;
  Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc& desc) override;
  Result<std::unique_ptr<CommandList>> create_command_list() override {
    return std::make_unique<WebGLCommandList>(this);
  }
  Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc& desc) override;
  Result<std::unique_ptr<Fence>> create_fence() override {
    return std::make_unique<WebGLFence>();
  }

  Result<void> begin_frame(SwapChain&) override { return make_current(); }
  Result<void> execute(CommandList&) override { return {}; }
  Result<void> end_frame(SwapChain&) override {
    emscripten_webgl_commit_frame();
    return {};
  }
  void wait_idle() override { glFinish(); }

  Result<void> make_current();
  [[nodiscard]] GLuint push_ubo() const { return push_ubo_; }
  [[nodiscard]] GLuint vao() const { return vao_; }

 private:
  void destroy();

  DeviceCreateInfo info_{};
  EMSCRIPTEN_WEBGL_CONTEXT_HANDLE context_ = 0;
  GLuint push_ubo_ = 0;
  GLuint vao_ = 0;
};

void WebGLCommandList::begin_render_pass(SwapChain&, const float clear_color[4],
                                         float clear_depth) {
  glEnable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glDepthFunc(GL_LESS);
  glDepthMask(GL_TRUE);
  glClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
  glClearDepthf(clear_depth);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void WebGLCommandList::set_pipeline(PipelineState& pipeline) {
  pipeline_ = static_cast<WebGLPipeline*>(&pipeline);
  glUseProgram(pipeline_->program());
  if (pipeline_->depth_test()) {
    glEnable(GL_DEPTH_TEST);
  } else {
    glDisable(GL_DEPTH_TEST);
  }
  glDepthMask(pipeline_->depth_write() ? GL_TRUE : GL_FALSE);
}

void WebGLCommandList::set_push_constants(std::span<const std::byte> data) {
  glBindBuffer(GL_UNIFORM_BUFFER, device_->push_ubo());
  glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(data.size()), data.data());
  glBindBufferBase(GL_UNIFORM_BUFFER, kPushConstantBinding, device_->push_ubo());
}

void WebGLCommandList::draw_indexed(const DrawIndexedDesc& desc) {
  if (!pipeline_ || !vertex_ || !index_) {
    return;
  }
  glBindVertexArray(device_->vao());
  glBindBuffer(GL_ARRAY_BUFFER, vertex_->handle());
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_->handle());
  constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
  const auto base = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertex_offset_));
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<const void*>(
          static_cast<std::uintptr_t>(vertex_offset_ + offsetof(Vertex, normal))));
  glEnableVertexAttribArray(2);
  glVertexAttribPointer(
      2, 2, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertex_offset_ + offsetof(Vertex, uv))));
  glEnableVertexAttribArray(3);
  glVertexAttribPointer(
      3, 3, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<const void*>(
          static_cast<std::uintptr_t>(vertex_offset_ + offsetof(Vertex, color))));
  const auto index_ptr = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
      index_offset_ + desc.first_index * sizeof(std::uint32_t)));
  const GLenum mode =
      pipeline_->topology() == PrimitiveTopology::LineList ? GL_LINES : GL_TRIANGLES;
  glDrawElements(mode, static_cast<GLsizei>(desc.index_count), GL_UNSIGNED_INT, index_ptr);
}

Result<void> WebGLDevice::make_current() {
  if (!context_) {
    return Err("WebGL context is not created");
  }
  if (emscripten_webgl_make_context_current(context_) != EMSCRIPTEN_RESULT_SUCCESS) {
    return Err("emscripten_webgl_make_context_current failed");
  }
  return {};
}

void WebGLDevice::destroy() {
  if (push_ubo_) {
    glDeleteBuffers(1, &push_ubo_);
    push_ubo_ = 0;
  }
  if (vao_) {
    glDeleteVertexArrays(1, &vao_);
    vao_ = 0;
  }
  if (context_) {
    emscripten_webgl_destroy_context(context_);
    context_ = 0;
  }
}

Result<void> WebGLDevice::initialize() {
  EmscriptenWebGLContextAttributes attr;
  emscripten_webgl_init_context_attributes(&attr);
  attr.majorVersion = 2;
  attr.minorVersion = 0;
  attr.alpha = EM_FALSE;
  attr.depth = EM_TRUE;
  attr.stencil = EM_FALSE;
  attr.antialias = EM_TRUE;
  attr.enableExtensionsByDefault = EM_TRUE;
  const char* selectors[] = {"#viewport", "canvas"};
  for (const char* sel : selectors) {
    attr.explicitSwapControl = EM_TRUE;
    context_ = emscripten_webgl_create_context(sel, &attr);
    if (context_ > 0) {
      break;
    }
    attr.explicitSwapControl = EM_FALSE;
    context_ = emscripten_webgl_create_context(sel, &attr);
    if (context_ > 0) {
      break;
    }
  }
  if (context_ <= 0) {
    return Err("failed to create WebGL2 context");
  }
  if (auto r = make_current(); !r) {
    return r;
  }
  glGenBuffers(1, &push_ubo_);
  glBindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
  glBufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
  glGenVertexArrays(1, &vao_);
  log_info("WebGL2 RHI initialized");
  return {};
}

Result<std::unique_ptr<Buffer>> WebGLDevice::create_buffer(const BufferDesc& desc) {
  if (auto r = make_current(); !r) {
    return Err(r.error());
  }
  GLenum target = GL_ARRAY_BUFFER;
  if (any(desc.usage, BufferDesc::Usage::Index)) {
    target = GL_ELEMENT_ARRAY_BUFFER;
  } else if (any(desc.usage, BufferDesc::Usage::Uniform)) {
    target = GL_UNIFORM_BUFFER;
  }
  GLuint buffer = 0;
  glGenBuffers(1, &buffer);
  glBindBuffer(target, buffer);
  glBufferData(target, static_cast<GLsizeiptr>(desc.size), nullptr,
               desc.host_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
  return std::make_unique<WebGLBuffer>(buffer, desc, target);
}

Result<std::unique_ptr<Texture>> WebGLDevice::create_texture(const TextureDesc& desc) {
  if (auto r = make_current(); !r) {
    return Err(r.error());
  }
  GLuint texture = 0;
  glGenTextures(1, &texture);
  glBindTexture(GL_TEXTURE_2D, texture);
  const GLint internal = GL_SRGB8_ALPHA8;
  glTexImage2D(GL_TEXTURE_2D, 0, internal, static_cast<GLsizei>(desc.width),
               static_cast<GLsizei>(desc.height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
  return std::make_unique<WebGLTexture>(desc, texture);
}

Result<std::unique_ptr<ShaderModule>> WebGLDevice::create_shader_module(
    const ShaderModuleDesc& desc) {
  if (desc.language != ShaderLanguage::Glsl || desc.glsl.empty()) {
    return Err("WebGL shaders require GLSL ES source");
  }
  if (auto r = make_current(); !r) {
    return Err(r.error());
  }
  const GLenum type = desc.stage == ShaderStage::Fragment ? GL_FRAGMENT_SHADER : GL_VERTEX_SHADER;
  const GLuint shader = glCreateShader(type);
  const GLchar* src = desc.glsl.data();
  const GLint len = static_cast<GLint>(desc.glsl.size());
  glShaderSource(shader, 1, &src, &len);
  glCompileShader(shader);
  GLint status = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    GLint log_len = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
    std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
    glGetShaderInfoLog(shader, log_len, nullptr, log.data());
    glDeleteShader(shader);
    return Err(std::string("GLSL compile failed: ") + log.c_str());
  }
  return std::make_unique<WebGLShaderModule>(shader);
}

Result<std::unique_ptr<PipelineState>> WebGLDevice::create_pipeline(const PipelineDesc& desc) {
  auto* vs = static_cast<WebGLShaderModule*>(desc.vertex_shader);
  auto* fs = static_cast<WebGLShaderModule*>(desc.fragment_shader);
  if (!vs || !fs) {
    return Err("pipeline requires vertex and fragment shaders");
  }
  if (auto r = make_current(); !r) {
    return Err(r.error());
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, vs->handle());
  glAttachShader(program, fs->handle());
  glLinkProgram(program);
  GLint status = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    GLint log_len = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
    std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
    glGetProgramInfoLog(program, log_len, nullptr, log.data());
    glDeleteProgram(program);
    return Err(std::string("program link failed: ") + log.c_str());
  }
  const GLuint block = glGetUniformBlockIndex(program, "PushConstants");
  if (block != GL_INVALID_INDEX) {
    glUniformBlockBinding(program, block, kPushConstantBinding);
  }
  const GLint albedo = glGetUniformLocation(program, "albedo_tex");
  if (albedo >= 0) {
    glUseProgram(program);
    glUniform1i(albedo, 0);
    glUseProgram(0);
  }
  return std::make_unique<WebGLPipeline>(program, desc.depth_test, desc.depth_write,
                                         desc.topology);
}

Result<std::unique_ptr<SwapChain>> WebGLDevice::create_swap_chain(const SwapChainDesc& desc) {
  if (!desc.window.valid()) {
    return Err("invalid canvas for WebGL swapchain");
  }
  return std::make_unique<WebGLSwapChain>(desc);
}

Result<std::unique_ptr<RHIDevice>> create_webgl_device(const DeviceCreateInfo& info) {
  auto device = std::make_unique<WebGLDevice>(info);
  if (auto r = device->initialize(); !r) {
    return Err(r.error());
  }
  return device;
}

}  // namespace

void register_webgl_backend() {
  BackendModule module{};
  module.backend = GraphicsBackend::WebGL;
  module.create = create_webgl_device;
  register_backend(std::move(module));
}

}  // namespace tamias
