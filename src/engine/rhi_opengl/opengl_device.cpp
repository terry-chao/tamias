#include "opengl_backend.h"

#include "core/log.h"
#include "gl_loader.h"
#include "graphics/mesh.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <X11/Xlib.h>
#include <GL/glx.h>
#endif

namespace tamias {
namespace {

constexpr GLuint kPushConstantBinding = 0;

class OpenGLDevice;

class OpenGLBuffer final : public Buffer {
 public:
  OpenGLBuffer(GLuint buffer, BufferDesc desc, GLenum target)
      : buffer_(buffer), desc_(desc), target_(target) {}
  ~OpenGLBuffer() override {
    if (buffer_) {
      gl::DeleteBuffers(1, &buffer_);
    }
  }

  [[nodiscard]] const BufferDesc& desc() const override { return desc_; }
  [[nodiscard]] GLuint handle() const { return buffer_; }
  [[nodiscard]] GLenum target() const { return target_; }

  Result<void> write(std::uint64_t offset, std::span<const std::byte> data) override {
    if (!desc_.host_visible) {
      return Err("buffer is not host visible");
    }
    gl::BindBuffer(target_, buffer_);
    gl::BufferSubData(target_, static_cast<GLintptr>(offset),
                      static_cast<GLsizeiptr>(data.size()), data.data());
    return {};
  }

 private:
  GLuint buffer_ = 0;
  BufferDesc desc_{};
  GLenum target_ = GL_ARRAY_BUFFER;
};

class OpenGLShaderModule final : public ShaderModule {
 public:
  explicit OpenGLShaderModule(GLuint shader) : shader_(shader) {}
  ~OpenGLShaderModule() override {
    if (shader_) {
      gl::DeleteShader(shader_);
    }
  }
  [[nodiscard]] GLuint handle() const { return shader_; }

 private:
  GLuint shader_ = 0;
};

class OpenGLPipeline final : public PipelineState {
 public:
  OpenGLPipeline(GLuint program, bool wireframe, bool depth_test)
      : program_(program), wireframe_(wireframe), depth_test_(depth_test) {}
  ~OpenGLPipeline() override {
    if (program_) {
      gl::DeleteProgram(program_);
    }
  }
  [[nodiscard]] GLuint program() const { return program_; }
  [[nodiscard]] bool wireframe() const { return wireframe_; }
  [[nodiscard]] bool depth_test() const { return depth_test_; }

 private:
  GLuint program_ = 0;
  bool wireframe_ = false;
  bool depth_test_ = true;
};

class OpenGLFence final : public Fence {
 public:
  void wait() override { gl::Finish(); }
  void reset() override {}
};

class OpenGLSwapChain final : public SwapChain {
 public:
  OpenGLSwapChain(OpenGLDevice* device, SwapChainDesc desc);
  ~OpenGLSwapChain() override;

  Result<void> resize(std::uint32_t width, std::uint32_t height) override;
  [[nodiscard]] std::uint32_t width() const override { return width_; }
  [[nodiscard]] std::uint32_t height() const override { return height_; }
  [[nodiscard]] TextureDesc::Format color_format() const override {
    return TextureDesc::Format::B8G8R8A8_SRGB;
  }

  Result<void> make_current();
  Result<void> present();

 private:
  friend class OpenGLDevice;
  OpenGLDevice* device_ = nullptr;
  NativeWindowHandle window_{};
  std::uint32_t width_ = 1;
  std::uint32_t height_ = 1;
#if defined(_WIN32)
  HDC hdc_ = nullptr;
#else
  // GLX uses the device context + window window.
#endif
};

class OpenGLCommandList final : public CommandList {
 public:
  explicit OpenGLCommandList(OpenGLDevice* device) : device_(device) {}

  void begin() override { recording_ = true; }
  void end() override { recording_ = false; }

  void begin_render_pass(SwapChain& swap_chain, const float clear_color[4],
                         float clear_depth) override;
  void end_render_pass() override {}
  void set_pipeline(PipelineState& pipeline) override;
  void set_vertex_buffer(Buffer& buffer, std::uint64_t offset = 0) override;
  void set_index_buffer(Buffer& buffer, std::uint64_t offset = 0) override;
  void set_push_constants(std::span<const std::byte> data) override;
  void draw_indexed(const DrawIndexedDesc& desc) override;
  void set_viewport(float x, float y, float w, float h, float min_depth = 0.f,
                    float max_depth = 1.f) override;
  void set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w, std::uint32_t h) override;

 private:
  OpenGLDevice* device_ = nullptr;
  bool recording_ = false;
  OpenGLPipeline* pipeline_ = nullptr;
  OpenGLBuffer* vertex_ = nullptr;
  OpenGLBuffer* index_ = nullptr;
  std::uint64_t vertex_offset_ = 0;
  std::uint64_t index_offset_ = 0;
};

class OpenGLDevice final : public RHIDevice {
 public:
  explicit OpenGLDevice(DeviceCreateInfo info) : info_(info) {}
  ~OpenGLDevice() override { destroy(); }

  Result<void> initialize();

  [[nodiscard]] GraphicsBackend backend() const override { return GraphicsBackend::OpenGL; }
  [[nodiscard]] Mat4 clip_space_correction_matrix() const override { return Mat4::identity(); }

  Result<std::unique_ptr<Buffer>> create_buffer(const BufferDesc& desc) override;
  Result<std::unique_ptr<Texture>> create_texture(const TextureDesc&) override {
    return Err("create_texture not used yet");
  }
  Result<std::unique_ptr<ShaderModule>> create_shader_module(const ShaderModuleDesc& desc) override;
  Result<std::unique_ptr<PipelineState>> create_pipeline(const PipelineDesc& desc) override;
  Result<std::unique_ptr<CommandList>> create_command_list() override {
    return std::make_unique<OpenGLCommandList>(this);
  }
  Result<std::unique_ptr<SwapChain>> create_swap_chain(const SwapChainDesc& desc) override;
  Result<std::unique_ptr<Fence>> create_fence() override { return std::make_unique<OpenGLFence>(); }

  Result<void> begin_frame(SwapChain& swap_chain) override;
  Result<void> execute(CommandList&) override { return {}; }
  Result<void> end_frame(SwapChain& swap_chain) override;
  void wait_idle() override {
    if (make_current_dummy()) {
      gl::Finish();
      release_current();
    }
  }

  Result<void> make_current_dummy();
  Result<void> make_current_window(const NativeWindowHandle& window
#if defined(_WIN32)
                                   ,
                                   HDC* out_hdc
#endif
  );
  void release_current();
  [[nodiscard]] GLuint push_ubo() const { return push_ubo_; }
  [[nodiscard]] GLuint vao() const { return vao_; }

 private:
  friend class OpenGLSwapChain;
  friend class OpenGLCommandList;

  void destroy();
  Result<void> create_dummy_context();
  Result<GLuint> compile_shader(GLenum type, std::span<const char> source);

  DeviceCreateInfo info_{};
  GLuint push_ubo_ = 0;
  GLuint vao_ = 0;
  bool procs_loaded_ = false;

#if defined(_WIN32)
  HWND dummy_hwnd_ = nullptr;
  HDC dummy_hdc_ = nullptr;
  HGLRC context_ = nullptr;
#else
  Display* display_ = nullptr;
  Window dummy_window_ = 0;
  GLXContext context_ = nullptr;
  Colormap colormap_ = 0;
  XVisualInfo* visual_ = nullptr;
  bool owns_display_ = false;
#endif
};

#if defined(_WIN32)

constexpr int WGL_CONTEXT_MAJOR_VERSION_ARB = 0x2091;
constexpr int WGL_CONTEXT_MINOR_VERSION_ARB = 0x2092;
constexpr int WGL_CONTEXT_PROFILE_MASK_ARB = 0x9126;
constexpr int WGL_CONTEXT_CORE_PROFILE_BIT_ARB = 0x00000001;
constexpr int WGL_DRAW_TO_WINDOW_ARB = 0x2001;
constexpr int WGL_SUPPORT_OPENGL_ARB = 0x2010;
constexpr int WGL_DOUBLE_BUFFER_ARB = 0x2011;
constexpr int WGL_PIXEL_TYPE_ARB = 0x2013;
constexpr int WGL_TYPE_RGBA_ARB = 0x202B;
constexpr int WGL_COLOR_BITS_ARB = 0x2014;
constexpr int WGL_DEPTH_BITS_ARB = 0x2022;

Result<void> set_pixel_format(HDC hdc) {
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;

  int format = 0;
  if (gl::ChoosePixelFormatARB) {
    const int attribs[] = {WGL_DRAW_TO_WINDOW_ARB,
                           1,
                           WGL_SUPPORT_OPENGL_ARB,
                           1,
                           WGL_DOUBLE_BUFFER_ARB,
                           1,
                           WGL_PIXEL_TYPE_ARB,
                           WGL_TYPE_RGBA_ARB,
                           WGL_COLOR_BITS_ARB,
                           32,
                           WGL_DEPTH_BITS_ARB,
                           24,
                           0};
    UINT count = 0;
    if (!gl::ChoosePixelFormatARB(hdc, attribs, nullptr, 1, &format, &count) || count == 0) {
      format = ChoosePixelFormat(hdc, &pfd);
    }
  } else {
    format = ChoosePixelFormat(hdc, &pfd);
  }
  if (format == 0 || !SetPixelFormat(hdc, format, &pfd)) {
    return Err("SetPixelFormat failed");
  }
  return {};
}

Result<void> OpenGLDevice::create_dummy_context() {
  dummy_hwnd_ = CreateWindowExA(0, "STATIC", "tamias_gl_dummy", WS_POPUP, 0, 0, 1, 1, nullptr,
                                nullptr, GetModuleHandleA(nullptr), nullptr);
  if (!dummy_hwnd_) {
    return Err("CreateWindowEx for OpenGL dummy failed");
  }
  dummy_hdc_ = GetDC(dummy_hwnd_);
  if (!dummy_hdc_) {
    return Err("GetDC for OpenGL dummy failed");
  }

  // Bootstrap legacy context to load WGL extensions.
  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;
  const int bootstrap_format = ChoosePixelFormat(dummy_hdc_, &pfd);
  if (bootstrap_format == 0 || !SetPixelFormat(dummy_hdc_, bootstrap_format, &pfd)) {
    return Err("bootstrap SetPixelFormat failed");
  }
  HGLRC bootstrap = wglCreateContext(dummy_hdc_);
  if (!bootstrap || !wglMakeCurrent(dummy_hdc_, bootstrap)) {
    if (bootstrap) {
      wglDeleteContext(bootstrap);
    }
    return Err("bootstrap wglCreateContext failed");
  }
  if (!gl::load_procs()) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(bootstrap);
    return Err("failed to load OpenGL entry points");
  }
  procs_loaded_ = true;

  const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                         4,
                         WGL_CONTEXT_MINOR_VERSION_ARB,
                         5,
                         WGL_CONTEXT_PROFILE_MASK_ARB,
                         WGL_CONTEXT_CORE_PROFILE_BIT_ARB,
                         0};
  context_ = gl::CreateContextAttribsARB(dummy_hdc_, nullptr, attribs);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(bootstrap);
  if (!context_) {
    return Err("wglCreateContextAttribsARB failed (need OpenGL 4.5)");
  }
  if (!wglMakeCurrent(dummy_hdc_, context_)) {
    return Err("wglMakeCurrent dummy failed");
  }

  gl::GenBuffers(1, &push_ubo_);
  gl::BindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
  gl::BufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
  gl::GenVertexArrays(1, &vao_);

  wglMakeCurrent(nullptr, nullptr);
  return {};
}

Result<void> OpenGLDevice::make_current_dummy() {
  if (!wglMakeCurrent(dummy_hdc_, context_)) {
    return Err("wglMakeCurrent dummy failed");
  }
  return {};
}

Result<void> OpenGLDevice::make_current_window(const NativeWindowHandle& window, HDC* out_hdc) {
  if (!window.hwnd) {
    return Err("OpenGL swapchain missing HWND");
  }
  HWND hwnd = static_cast<HWND>(window.hwnd);
  HDC hdc = GetDC(hwnd);
  if (!hdc) {
    return Err("GetDC failed for swapchain window");
  }
  // Pixel format can only be set once per window; ignore failure if already set.
  (void)set_pixel_format(hdc);
  if (!wglMakeCurrent(hdc, context_)) {
    ReleaseDC(hwnd, hdc);
    return Err("wglMakeCurrent swapchain failed");
  }
  if (out_hdc) {
    *out_hdc = hdc;
  }
  return {};
}

void OpenGLDevice::release_current() { wglMakeCurrent(nullptr, nullptr); }

void OpenGLDevice::destroy() {
  if (context_) {
    wglMakeCurrent(dummy_hdc_, context_);
    if (vao_) {
      gl::DeleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (push_ubo_) {
      gl::DeleteBuffers(1, &push_ubo_);
      push_ubo_ = 0;
    }
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(context_);
    context_ = nullptr;
  }
  if (dummy_hdc_ && dummy_hwnd_) {
    ReleaseDC(dummy_hwnd_, dummy_hdc_);
    dummy_hdc_ = nullptr;
  }
  if (dummy_hwnd_) {
    DestroyWindow(dummy_hwnd_);
    dummy_hwnd_ = nullptr;
  }
}

#else  // Linux GLX

#ifndef GLX_CONTEXT_MAJOR_VERSION_ARB
#define GLX_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define GLX_CONTEXT_MINOR_VERSION_ARB 0x2092
#define GLX_CONTEXT_PROFILE_MASK_ARB 0x9126
#define GLX_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#endif

using PFN_glXCreateContextAttribsARB = GLXContext (*)(Display*, GLXFBConfig, GLXContext, Bool,
                                                      const int*);

Result<void> OpenGLDevice::create_dummy_context() {
  display_ = XOpenDisplay(nullptr);
  if (!display_) {
    return Err("XOpenDisplay failed for OpenGL");
  }
  owns_display_ = true;

  static int fb_attribs[] = {GLX_X_RENDERABLE,
                             True,
                             GLX_DRAWABLE_TYPE,
                             GLX_WINDOW_BIT,
                             GLX_RENDER_TYPE,
                             GLX_RGBA_BIT,
                             GLX_X_VISUAL_TYPE,
                             GLX_TRUE_COLOR,
                             GLX_RED_SIZE,
                             8,
                             GLX_GREEN_SIZE,
                             8,
                             GLX_BLUE_SIZE,
                             8,
                             GLX_DEPTH_SIZE,
                             24,
                             GLX_DOUBLEBUFFER,
                             True,
                             None};
  int fb_count = 0;
  GLXFBConfig* fbc = glXChooseFBConfig(display_, DefaultScreen(display_), fb_attribs, &fb_count);
  if (!fbc || fb_count == 0) {
    return Err("glXChooseFBConfig failed");
  }
  GLXFBConfig config = fbc[0];
  visual_ = glXGetVisualFromFBConfig(display_, config);
  if (!visual_) {
    XFree(fbc);
    return Err("glXGetVisualFromFBConfig failed");
  }
  colormap_ = XCreateColormap(display_, RootWindow(display_, visual_->screen), visual_->visual,
                              AllocNone);
  XSetWindowAttributes swa{};
  swa.colormap = colormap_;
  swa.event_mask = 0;
  dummy_window_ =
      XCreateWindow(display_, RootWindow(display_, visual_->screen), 0, 0, 1, 1, 0, visual_->depth,
                    InputOutput, visual_->visual, CWColormap | CWEventMask, &swa);
  if (!dummy_window_) {
    XFree(fbc);
    return Err("XCreateWindow dummy failed");
  }

  auto create_attribs = reinterpret_cast<PFN_glXCreateContextAttribsARB>(
      glXGetProcAddressARB(reinterpret_cast<const GLubyte*>("glXCreateContextAttribsARB")));
  if (!create_attribs) {
    XFree(fbc);
    return Err("glXCreateContextAttribsARB unavailable");
  }
  const int ctx_attribs[] = {GLX_CONTEXT_MAJOR_VERSION_ARB,
                             4,
                             GLX_CONTEXT_MINOR_VERSION_ARB,
                             5,
                             GLX_CONTEXT_PROFILE_MASK_ARB,
                             GLX_CONTEXT_CORE_PROFILE_BIT_ARB,
                             None};
  context_ = create_attribs(display_, config, nullptr, True, ctx_attribs);
  XFree(fbc);
  if (!context_) {
    return Err("glXCreateContextAttribsARB failed (need OpenGL 4.5)");
  }
  if (!glXMakeCurrent(display_, dummy_window_, context_)) {
    return Err("glXMakeCurrent dummy failed");
  }
  if (!gl::load_procs()) {
    glXMakeCurrent(display_, None, nullptr);
    return Err("failed to load OpenGL entry points");
  }
  procs_loaded_ = true;

  gl::GenBuffers(1, &push_ubo_);
  gl::BindBuffer(GL_UNIFORM_BUFFER, push_ubo_);
  gl::BufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
  gl::GenVertexArrays(1, &vao_);

  glXMakeCurrent(display_, None, nullptr);
  return {};
}

Result<void> OpenGLDevice::make_current_dummy() {
  if (!glXMakeCurrent(display_, dummy_window_, context_)) {
    return Err("glXMakeCurrent dummy failed");
  }
  return {};
}

Result<void> OpenGLDevice::make_current_window(const NativeWindowHandle& window) {
  if (!window.display || window.window == 0) {
    return Err("OpenGL swapchain missing X11 display/window");
  }
  auto* display = static_cast<Display*>(window.display);
  if (!glXMakeCurrent(display, static_cast<Window>(window.window), context_)) {
    return Err("glXMakeCurrent swapchain failed");
  }
  // Prefer the view's display for present; keep a pointer for SwapBuffers.
  display_ = display;
  owns_display_ = false;
  return {};
}

void OpenGLDevice::release_current() {
  if (display_) {
    glXMakeCurrent(display_, None, nullptr);
  }
}

void OpenGLDevice::destroy() {
  if (context_ && display_) {
    glXMakeCurrent(display_, dummy_window_, context_);
    if (vao_) {
      gl::DeleteVertexArrays(1, &vao_);
      vao_ = 0;
    }
    if (push_ubo_) {
      gl::DeleteBuffers(1, &push_ubo_);
      push_ubo_ = 0;
    }
    glXMakeCurrent(display_, None, nullptr);
    glXDestroyContext(display_, context_);
    context_ = nullptr;
  }
  if (dummy_window_ && display_) {
    XDestroyWindow(display_, dummy_window_);
    dummy_window_ = 0;
  }
  if (colormap_ && display_) {
    XFreeColormap(display_, colormap_);
    colormap_ = 0;
  }
  if (visual_) {
    XFree(visual_);
    visual_ = nullptr;
  }
  if (owns_display_ && display_) {
    XCloseDisplay(display_);
  }
  display_ = nullptr;
}

#endif

Result<void> OpenGLDevice::initialize() {
  if (auto r = create_dummy_context(); !r) {
    return r;
  }
  log_info("OpenGL RHI initialized");
  return {};
}

Result<GLuint> OpenGLDevice::compile_shader(GLenum type, std::span<const char> source) {
  if (auto r = make_current_dummy(); !r) {
    return Err(r.error());
  }
  const GLuint shader = gl::CreateShader(type);
  const GLchar* src = source.data();
  const GLint len = static_cast<GLint>(source.size());
  gl::ShaderSource(shader, 1, &src, &len);
  gl::CompileShader(shader);
  GLint status = 0;
  gl::GetShaderiv(shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    GLint log_len = 0;
    gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_len);
    std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
    gl::GetShaderInfoLog(shader, log_len, nullptr, log.data());
    gl::DeleteShader(shader);
    release_current();
    return Err(std::string("shader compile failed: ") + log.c_str());
  }
  release_current();
  return shader;
}

Result<std::unique_ptr<Buffer>> OpenGLDevice::create_buffer(const BufferDesc& desc) {
  if (auto r = make_current_dummy(); !r) {
    return Err(r.error());
  }
  GLenum target = GL_ARRAY_BUFFER;
  if (any(desc.usage, BufferDesc::Usage::Index)) {
    target = GL_ELEMENT_ARRAY_BUFFER;
  } else if (any(desc.usage, BufferDesc::Usage::Uniform)) {
    target = GL_UNIFORM_BUFFER;
  }
  GLuint buffer = 0;
  gl::GenBuffers(1, &buffer);
  gl::BindBuffer(target, buffer);
  gl::BufferData(target, static_cast<GLsizeiptr>(desc.size), nullptr,
                 desc.host_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
  release_current();
  return std::make_unique<OpenGLBuffer>(buffer, desc, target);
}

Result<std::unique_ptr<ShaderModule>> OpenGLDevice::create_shader_module(
    const ShaderModuleDesc& desc) {
  if (desc.language != ShaderLanguage::Glsl || desc.glsl.empty()) {
    return Err("OpenGL shaders require GLSL source");
  }
  // Stage is inferred later at pipeline link; compile as vertex first if unknown.
  // RenderThread creates separate modules; create_pipeline recompiles from stored source.
  // Store precompiled? We need stage. Heuristic: look for "gl_Position" => vertex.
  const bool is_vertex = std::string_view(desc.glsl.data(), desc.glsl.size()).find("gl_Position") !=
                         std::string_view::npos;
  auto shader = compile_shader(is_vertex ? GL_VERTEX_SHADER : GL_FRAGMENT_SHADER, desc.glsl);
  if (!shader) {
    return Err(shader.error());
  }
  return std::make_unique<OpenGLShaderModule>(*shader);
}

Result<std::unique_ptr<PipelineState>> OpenGLDevice::create_pipeline(const PipelineDesc& desc) {
  auto* vs = static_cast<OpenGLShaderModule*>(desc.vertex_shader);
  auto* fs = static_cast<OpenGLShaderModule*>(desc.fragment_shader);
  if (!vs || !fs) {
    return Err("pipeline requires vertex and fragment shaders");
  }
  if (auto r = make_current_dummy(); !r) {
    return Err(r.error());
  }
  const GLuint program = gl::CreateProgram();
  gl::AttachShader(program, vs->handle());
  gl::AttachShader(program, fs->handle());
  gl::LinkProgram(program);
  GLint status = 0;
  gl::GetProgramiv(program, GL_LINK_STATUS, &status);
  if (!status) {
    GLint log_len = 0;
    gl::GetProgramiv(program, GL_INFO_LOG_LENGTH, &log_len);
    std::string log(static_cast<std::size_t>(std::max(log_len, 1)), '\0');
    gl::GetProgramInfoLog(program, log_len, nullptr, log.data());
    gl::DeleteProgram(program);
    release_current();
    return Err(std::string("program link failed: ") + log.c_str());
  }
  release_current();
  return std::make_unique<OpenGLPipeline>(program, desc.wireframe, desc.depth_test);
}

OpenGLSwapChain::OpenGLSwapChain(OpenGLDevice* device, SwapChainDesc desc)
    : device_(device), window_(desc.window), width_(std::max(1u, desc.width)),
      height_(std::max(1u, desc.height)) {}

OpenGLSwapChain::~OpenGLSwapChain() {
#if defined(_WIN32)
  if (hdc_ && window_.hwnd) {
    ReleaseDC(static_cast<HWND>(window_.hwnd), hdc_);
    hdc_ = nullptr;
  }
#endif
}

Result<void> OpenGLSwapChain::resize(std::uint32_t width, std::uint32_t height) {
  width_ = std::max(1u, width);
  height_ = std::max(1u, height);
  return {};
}

Result<void> OpenGLSwapChain::make_current() {
#if defined(_WIN32)
  if (hdc_) {
    ReleaseDC(static_cast<HWND>(window_.hwnd), hdc_);
    hdc_ = nullptr;
  }
  return device_->make_current_window(window_, &hdc_);
#else
  return device_->make_current_window(window_);
#endif
}

Result<void> OpenGLSwapChain::present() {
#if defined(_WIN32)
  if (!hdc_ || !SwapBuffers(hdc_)) {
    return Err("SwapBuffers failed");
  }
#else
  if (!device_ || !window_.display) {
    return Err("GLX present missing display");
  }
  glXSwapBuffers(static_cast<Display*>(window_.display), static_cast<Window>(window_.window));
#endif
  return {};
}

Result<std::unique_ptr<SwapChain>> OpenGLDevice::create_swap_chain(const SwapChainDesc& desc) {
  if (!desc.window.valid()) {
    return Err("invalid native window for OpenGL swapchain");
  }
  return std::make_unique<OpenGLSwapChain>(this, desc);
}

Result<void> OpenGLDevice::begin_frame(SwapChain& swap_chain) {
  auto& sc = static_cast<OpenGLSwapChain&>(swap_chain);
  return sc.make_current();
}

Result<void> OpenGLDevice::end_frame(SwapChain& swap_chain) {
  auto& sc = static_cast<OpenGLSwapChain&>(swap_chain);
  auto r = sc.present();
  release_current();
  return r;
}

void OpenGLCommandList::begin_render_pass(SwapChain&, const float clear_color[4],
                                          float clear_depth) {
  gl::Enable(GL_DEPTH_TEST);
  gl::Enable(GL_SCISSOR_TEST);
  gl::DepthFunc(GL_LESS);
  gl::DepthMask(GL_TRUE);
  gl::ClearColor(clear_color[0], clear_color[1], clear_color[2], clear_color[3]);
  gl::ClearDepth(clear_depth);
  gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void OpenGLCommandList::set_pipeline(PipelineState& pipeline) {
  pipeline_ = static_cast<OpenGLPipeline*>(&pipeline);
  gl::UseProgram(pipeline_->program());
  gl::PolygonMode(GL_FRONT_AND_BACK, pipeline_->wireframe() ? GL_LINE : GL_FILL);
  if (pipeline_->depth_test()) {
    gl::Enable(GL_DEPTH_TEST);
  } else {
    gl::Disable(GL_DEPTH_TEST);
  }
}

void OpenGLCommandList::set_vertex_buffer(Buffer& buffer, std::uint64_t offset) {
  vertex_ = static_cast<OpenGLBuffer*>(&buffer);
  vertex_offset_ = offset;
}

void OpenGLCommandList::set_index_buffer(Buffer& buffer, std::uint64_t offset) {
  index_ = static_cast<OpenGLBuffer*>(&buffer);
  index_offset_ = offset;
}

void OpenGLCommandList::set_push_constants(std::span<const std::byte> data) {
  gl::BindBuffer(GL_UNIFORM_BUFFER, device_->push_ubo());
  gl::BufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(data.size()), data.data());
  gl::BindBufferBase(GL_UNIFORM_BUFFER, kPushConstantBinding, device_->push_ubo());
}

void OpenGLCommandList::set_viewport(float x, float y, float w, float h, float, float) {
  gl::Viewport(static_cast<GLint>(x), static_cast<GLint>(y), static_cast<GLsizei>(w),
               static_cast<GLsizei>(h));
}

void OpenGLCommandList::set_scissor(std::int32_t x, std::int32_t y, std::uint32_t w,
                                    std::uint32_t h) {
  gl::Scissor(x, y, static_cast<GLsizei>(w), static_cast<GLsizei>(h));
}

void OpenGLCommandList::draw_indexed(const DrawIndexedDesc& desc) {
  if (!pipeline_ || !vertex_ || !index_) {
    return;
  }
  gl::BindVertexArray(device_->vao());
  gl::BindBuffer(GL_ARRAY_BUFFER, vertex_->handle());
  gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_->handle());

  constexpr GLsizei stride = static_cast<GLsizei>(sizeof(Vertex));
  const auto base = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertex_offset_));
  gl::EnableVertexAttribArray(0);
  gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base);
  gl::EnableVertexAttribArray(1);
  gl::VertexAttribPointer(
      1, 3, GL_FLOAT, GL_FALSE, stride,
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertex_offset_ + sizeof(Vec3))));
  gl::EnableVertexAttribArray(2);
  gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
                              vertex_offset_ + sizeof(Vec3) + sizeof(Vec3))));

  const auto index_ptr =
      reinterpret_cast<const void*>(static_cast<std::uintptr_t>(
          index_offset_ + desc.first_index * sizeof(std::uint32_t)));
  gl::DrawElements(GL_TRIANGLES, static_cast<GLsizei>(desc.index_count), GL_UNSIGNED_INT,
                   index_ptr);
}

Result<std::unique_ptr<RHIDevice>> create_opengl_device(const DeviceCreateInfo& info) {
  auto device = std::make_unique<OpenGLDevice>(info);
  if (auto r = device->initialize(); !r) {
    return Err(r.error());
  }
  return device;
}

}  // namespace

void register_opengl_backend() {
  log_info("Registering OpenGL RHI backend");
  register_backend(BackendModule{GraphicsBackend::OpenGL, create_opengl_device});
}

}  // namespace tamias
