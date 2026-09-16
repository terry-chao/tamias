读完了这个文件（1157 行）以及它依赖的 `device.h`、`gl_loader.h`、渲染运行时和视口层。下面按「它在哪一层 → 文件地图 → 逐段拆解 → 上下文联动 → 坑」来讲。

---

# 一、这个文件在整个引擎里的位置

`opengl_device.cpp` 是 **RHI（Render Hardware Interface）四种实现之一**，是 GPU 后端的「方言翻译层」。

```
OCCT / OBJ / GLB  →  MeshCpu（CPU 顶点+索引）
Document          →  SceneDrawItem（谁、世界矩阵、材质）
DocumentViewport  →  FrameSubmission（提交帧）
RenderThread      →  只说 RHI 动词：create_buffer / set_texture / draw_indexed
                     ↓  RHIDevice 抽象接口（device.h）
opengl_device.cpp →  glGenBuffers / glBindBuffer / glDrawElements / SwapBuffers
```

关键点：**上层 `render_runtime.cpp` 的绘制剧本对四种后端是同一套 C++**，`device.h` 定义了全部动词。切到 OpenGL 时，变的只有这个文件里的 `gl*` 调用。所以这个文件**不做**这些事：不遍历场景图、不做视锥剔除、不找三角面、不做点选、不做合批——三角在进 GPU 之前就已经在索引缓冲里排好了。

同族的另外三份实现，可以对照理解：

| 文件 | 后端 | 特点 |
|---|---|---|
| [vulkan_device.cpp](C:\dev\tamias\src\engine\render\rhi\vulkan\vulkan_device.cpp) | Vulkan | 真正的命令缓冲录制，多视口可共享线程 |
| [webgpu_device.cpp](C:\dev\tamias\src\engine\render\rhi\webgpu\webgpu_device.cpp) | WebGPU | WGSL，浏览器/原生 |
| [webgl_device.cpp](C:\dev\tamias\src\engine\render\rhi\webgl\webgl_device.cpp) | WebGL | GLSL 源码编译 |
| **opengl_device.cpp** | **OpenGL** | **GL 4.5 Core，立即模式，SPIR-V** |

有一个硬约束决定了它的全部结构：**OpenGL 的 context 是线程局部状态**。所以 `RenderDeviceConfig::shares_execution_thread_with()`（[render_runtime.h:73](C:\dev\tamias\src\engine\render\runtime\render_runtime.h:73)）对 OpenGL 永远返回 false —— 每个文档独占一条渲染线程 + 一个 GL context。

---

# 二、文件地图

| 行号 | 内容 |
|---|---|
| 1–18 | include 与平台分支（Win32: `windows.h`；Linux: `Xlib.h` + `GL/glx.h`） |
| 24 | `kPushConstantBinding = 0` |
| 28–44 | `OpenGLBuffer` |
| 46–63 | `OpenGLTexture` |
| 65–77 | `OpenGLShaderModule` |
| 79–111 | `OpenGLPipeline` |
| 113–117 | `OpenGLFence` |
| 119–145 | `OpenGLSwapChain` |
| 147–178 | `OpenGLCommandList` |
| 180–263 | `OpenGLDevice` 主体声明 |
| 265–395 | Buffer/Texture 的实现 + 格式映射表 |
| 397–583 | **Windows WGL 段**：dummy context、像素格式、make_current、destroy |
| 585–734 | **Linux GLX 段**：同样职责的 X11 版 |
| 737–830 | `initialize` / `create_buffer` / `create_shader_module` / `create_pipeline` |
| 832–902 | SwapChain + `begin_frame` / `end_frame` |
| 904–972 | CommandList 的立即状态设置 |
| 974–1044 | `draw_indexed`（真正发 draw call） |
| 1046–1051 | `create_opengl_device`（工厂） |
| 1053–1150 | Win32 专属：给 Qt 视口造子 HWND 的三个辅助函数 |
| 1154–1157 | `register_opengl_backend` |

---

# 三、逐段拆解

## 3.1 头部与平台分支（1–22）

```cpp
#include "opengl_backend.h"          // 对外暴露的注册/建表面函数
#include "engine/base/log.h"
#include "gl_loader.h"               // 自带的 GL 4.5 函数指针表
#include "engine/graphics/mesh.h"    // Vertex 布局，draw 时要按它设属性
#include "engine/render/runtime/gpu_instance.h"  // GpuInstance 布局
```

平台分支只为了拿 `HWND/HDC/HGLRC` 或 `Display*/Window/GLXContext`。所有 GL 函数本身都不直接调用系统符号，而是走 `tamias::gl::` 命名空间里的**函数指针**——因为 Windows 自带的 `opengl32.lib` 只到 GL 1.1，4.5 的入口必须运行时 `wglGetProcAddress` 取。这套加载器在 [gl_loader.h](C:\dev\tamias\src\engine\render\rhi\opengl\gl_loader.h) / [gl_loader.cpp](C:\dev\tamias\src\engine\render\rhi\opengl\gl_loader.cpp)。

## 3.2 `kPushConstantBinding = 0`（24）

一个语义映射点：RHI 接口里的 `set_push_constants` 在 GL 侧**没有 push constant 这种概念**，所以这里约定用 **UBO binding 0** 来模拟。shader 里写的 `[[vk::push_constant]]` / 或绑定 0 的 cbuffer，两边布局必须对齐。这个常量就是那个「0」。

## 3.3 六个资源包装类

GL 是 C 风格 API，只有整数句柄；RHI 要的是带析构的对象。这六个类就是「句柄 + 生命周期 + 状态」的壳。

**`OpenGLBuffer`（28–44）**

```cpp
GLuint buffer_;   GLenum target_;   BufferDesc desc_;
```

多存了一个 `target_`（`GL_ARRAY_BUFFER` / `GL_ELEMENT_ARRAY_BUFFER` / `GL_UNIFORM_BUFFER`），因为 GL 的 buffer 是**绑定点绑定的**，写数据时得知道绑到哪儿。这个 target 在 `create_buffer`（745）里根据 `BufferDesc::Usage` 决定。

`write()`（279）要求 `desc_.host_visible` 为真，否则报错——这是 RHI 层面的语义约束（Vulkan 那边是映射内存，GL 这边就是 `glBufferSubData`）。

**`OpenGLTexture`（46–63）**

存 `TextureDesc` + `GLuint`。没有 FBO，也不建 depth 纹理——GL 的深度缓冲来自默认帧缓冲的像素格式，不来自纹理。

- `write(offset, data)`（307）只允许 `offset == 0`，直接转发到 `write_subresource(0, 0, ...)`。
- `write_subresource(mip, layer, ...)`（343）是真正建纹理存储的地方。

**`OpenGLShaderModule`（65–77）**
只包一个 `GLuint shader_`，析构时 `DeleteShader`。注意 GL 的 program 和 shader 是两级的：shader 编译一次，可被多个 program attach。这里的 `ShaderModule` = 一个已特化的 shader 对象。

**`OpenGLPipeline`（79–111）**

```cpp
GLuint program_;
bool wireframe_, depth_test_, depth_write_, blend_;
PrimitiveTopology topology_;
bool instanced_;
```

**这个类是最能说明 GL 与 Vulkan 差异的地方。** Vulkan 有真正的 `VkPipeline` 对象，把所有固定功能状态烘进去；GL 没有（4.5 core 也没广泛可用的 pipeline object），所以这里把状态位**存在 C++ 对象里**，等到 `set_pipeline` 时再逐条 `glEnable/glDisable/glPolygonMode/glDepthMask`。这就是为什么这个类不是简单的 RAII 壳，而携带了语义。

`instanced_` 尤其重要：它不是 GL 状态，而是「本 pipeline 要不要读第二个顶点缓冲（`GpuInstance`）」的开关，直接决定 `draw_indexed` 走哪条分支。

**`OpenGLFence`（113–117）**

```cpp
void wait() override { gl::Finish(); }
void reset() override {}
```

**最粗暴的实现**：GL 没有 fence 对象语义，直接 `glFinish()` 等 GPU 全部干完。Vulkan 那边是 `VkFence` 真异步。这是「先保证正确，不追求性能」的取舍。

**`OpenGLSwapChain`（119–145）**

存 `NativeWindowHandle` + 尺寸，Win32 下多一个 `HDC hdc_`（每帧 GetDC 得来，析构 ReleaseDC）。`color_format()` 硬编码返回 `B8G8R8A8_SRGB`——只是为了跟别的后端在 RHI 语义上对齐，GL 侧其实不用这个值。

**`OpenGLCommandList`（147–178）**

注意它的成员全是**指针和偏移**，没有任何 GL 对象的立即创建：

```cpp
OpenGLPipeline* pipeline_;  OpenGLBuffer* vertex_, *instance_, *index_;
std::uint64_t vertex_offset_, instance_offset_, index_offset_;
```

`set_vertex_buffer` 这类调用**不产生任何 GL 调用**，只是记下来。真正的 `glBindBuffer` 在 `draw_indexed` 里做。这是有意的：**GL 没有独立的「命令缓冲」，所有调用立即执行**，所以在 `set_*` 时绑定会被下一次 `set_*` 覆盖，只有到 draw 那一刻的状态才是有效的。

`begin()/end()` 也只翻一个 `recording_` 布尔（而且这个布尔之后没被读过，纯粹是接口对齐）。

## 3.4 `OpenGLDevice` 声明（180–263）

这是整个后端的中枢。几个必须讲清楚的点：

**(a) `clip_space_correction_matrix()`（187–193）**

```cpp
// perspective() emits Z in [0,1] (Vulkan-style). Remap to OpenGL [-1,1].
Mat4 m = Mat4::identity();
m(2, 2) = 2.f;
m(2, 3) = -1.f;
```

CPU 端的 `perspective()`（[math.h:161](C:\dev\tamias\src\engine\math\math.h:161)）按 Vulkan 惯例输出 Z∈[0,1]。GL 的 NDC 是 Z∈[-1,1]，所以这里映射 `z' = 2z - 1`。在 [render_runtime.cpp:1031](C:\dev\tamias\src\engine\render\runtime\render_runtime.cpp:1031) 里：

```cpp
const Mat4 clip = device_->clip_space_correction_matrix();
const Mat4 view_proj = clip * frame.proj * frame.view;
```

对照：Vulkan 的版本是翻 Y（`m(1,1) = -1`），WebGPU 是只翻 Z。**三个后端各自的「坐标约定差异」都被收敛在这一个函数里**，上层完全不用管。

**(b) 为什么每个 `create_*` 都要先 `make_current_dummy()`**

```cpp
Result<std::unique_ptr<Texture>> create_texture(const TextureDesc& desc) override {
  if (auto r = make_current_dummy(); !r) return Err(r.error());
  GLuint texture = 0;
  gl::GenTextures(1, &texture);
  release_current();
  ...
}
```

GL 的所有调用都作用于「当前线程当前 current 的 context」。资源可能在任何时候创建（渲染线程遍历场景时动态上传），所以要**每次显式切到 dummy context，用完释放**。这套 `make_current_dummy / release_current` 配对在文件里出现了七八次，全是这个原因。

**(c) `execute()` 是空的（220）**

```cpp
Result<void> execute(CommandList&) override { return {}; }
```

因为 GL 是立即模式：`set_*` / `draw_indexed` 在调用时就已经发到 GPU 了，不存在「提交命令缓冲」这一步。[render_runtime.cpp:1473](C:\dev\tamias\src\engine\render\runtime\render_runtime.cpp:1473) 那一句 `device_->execute(*channel.command_list)` 对 OpenGL 是**空操作**，对 Vulkan 才是真正的 `vkQueueSubmit`。

## 3.5 Windows 的 dummy context：两段式 bootstrap（397–583）

这是整个文件最「讲究」的部分，也是 Windows 上最容易踩坑的地方。

```cpp
Result<void> OpenGLDevice::create_dummy_context() {
  // 1) 建一个 1×1 的隐藏 STATIC 窗口
  dummy_hwnd_ = CreateWindowExA(0, "STATIC", "tamias_gl_dummy", WS_POPUP, ...);
  dummy_hdc_ = GetDC(dummy_hwnd_);

  // 2) 先建一个「老式」context（GL 1.1 都行）
  SetPixelFormat(dummy_hdc_, bootstrap_format, &pfd);
  HGLRC bootstrap = wglCreateContext(dummy_hdc_);
  wglMakeCurrent(dummy_hdc_, bootstrap);

  // 3) 趁现在有 current context，才能取到 wgl*ARB 扩展入口
  gl::load_procs();          // ← 这里才把 gl_loader 的函数指针填满

  // 4) 有了 CreateContextAttribsARB，才能建真正的 4.5 Core context
  context_ = gl::CreateContextAttribsARB(dummy_hdc_, nullptr, attribs /* 4.5 core */);
  wglMakeCurrent(nullptr, nullptr);
  wglDeleteContext(bootstrap);
  wglMakeCurrent(dummy_hdc_, context_);

  // 5) 建全局共享资源
  gl::GenBuffers(1, &push_ubo_);
  gl::BufferData(GL_UNIFORM_BUFFER, 256, nullptr, GL_DYNAMIC_DRAW);
  gl::GenVertexArrays(1, &vao_);
}
```

**为什么必须两段式？** WGL 的扩展函数指针（`wglCreateContextAttribsARB`、`wglChoosePixelFormatARB`）**只能在一个 context 已经是 current 的时候**通过 `wglGetProcAddress` 拿到。所以顺序被系统 API 锁死：必须先用 `wglCreateContext` 造个临时的、才能问「支持不支持 4.5」。

**为什么需要 dummy 窗口？** 因为 context 必须绑在一个 DC 上。资源创建（上传 mesh/纹理）不应该画到可见窗口上，所以留一个 1×1 隐藏窗口常驻。

**`push_ubo_` 和 `vao_` 为什么在 device 级？** 因为整个后端共用：只有一个 push constant 缓冲区（256 字节，见下），只有一个 VAO（见 3.9）。

配套的几个函数：

- `set_pixel_format(hdc)`（410）：优先用 `ChoosePixelFormatARB` + 属性数组（带 32 位色 / 24 位深、双缓冲），失败回退到老式 `ChoosePixelFormat`。这是给可见窗口用的。
- `make_current_dummy()`（512）：`wglMakeCurrent(dummy_hdc_, context_)`。
- `make_current_hdc(hdc)`（519）：把同一个 context 绑到别的 DC 上（渲染线程绑到可见窗口时用）。
- `make_current_window(window, out_hdc)`（529）：`GetDC(hwnd)` + 尽力设像素格式 + `make_current_hdc`。**注意：Win32 版这个函数实际上没人调用**（`OpenGLSwapChain::make_current` 直接自己 GetDC + `make_current_hdc` 了），算是平行于 GLX 版留下的死代码，见第四节。
- `release_current()`（550）：`wglMakeCurrent(nullptr, nullptr)`。
- `destroy()`（552）：切回 dummy，删 VAO / UBO，删 context，ReleaseDC + DestroyWindow。

## 3.6 Linux GLX 版（585–734）

职责完全一样，只是 API 换了：

- `XOpenDisplay(nullptr)` → `glXChooseFBConfig` → `glXGetVisualFromFBConfig` → `XCreateColormap` → `XCreateWindow` 造 1×1 dummy 窗口
- context 属性数组同样是 `4.5 + CORE_PROFILE`
- `glXGetProcAddressARB("glXCreateContextAttribsARB")` 取扩展入口（GLX 不需要 bootstrap context，这点比 WGL 舒服）
- `make_current_window`（682）在这里**是活的**：它把 `display_` 换成视口的 display、`owns_display_ = false`，这样 `release_current` 知道该在哪个 display 上解绑。
- `destroy()` 要按顺序 XDestroyWindow → XFreeColormap → XFree(visual) → XCloseDisplay（仅当自己开的 display）。

## 3.7 资源创建实现（745–830）

**`create_buffer`（745）** — target 由 usage 决定：

```cpp
GLenum target = GL_ARRAY_BUFFER;
if (any(desc.usage, BufferDesc::Usage::Index))        target = GL_ELEMENT_ARRAY_BUFFER;
else if (any(desc.usage, BufferDesc::Usage::Uniform)) target = GL_UNIFORM_BUFFER;
gl::BufferData(target, size, nullptr, desc.host_visible ? GL_DYNAMIC_DRAW : GL_STATIC_DRAW);
```

`host_visible` 映射成 `GL_DYNAMIC_DRAW`（会被 CPU 反复写，比如每帧的 instance 数据），否则 `GL_STATIC_DRAW`。注意这里只分配不初始化，数据由后续 `write()` 填。

**`create_shader_module`（764）** — 强制 SPIR-V：

```cpp
if (desc.language != ShaderLanguage::Spirv || desc.spirv.empty())
  return Err("OpenGL shaders require SPIR-V");
if (!gl::ShaderBinary || !gl::SpecializeShader)
  return Err("OpenGL SPIR-V unsupported (need GL_ARB_gl_spirv / glSpecializeShader)");
```

流程：`glCreateShader` → `glShaderBinary(GL_SHADER_BINARY_FORMAT_SPIR_V)` → `glSpecializeShader(entry)` → 检查 `GL_COMPILE_STATUS` → 取 info log。**不是运行时编译 GLSL**，用的是扩展 `GL_ARB_gl_spirv`。这要求显卡/驱动支持，否则这里报错。

对应地，[render_runtime.cpp:568](C:\dev\tamias\src\engine\render\runtime\render_runtime.cpp:568) 附近会根据后端选文件名：

```cpp
const char* vs_name = opengl ? "mesh.vert.gl.spv" : "mesh.vert.spv";
```

`*.gl.spv` 是同一份 HLSL 编译出来的、OpenGL 变体（由 [TamiasShaders.cmake](C:\dev\tamias\cmake\TamiasShaders.cmake) 生成，产物在 `build/shaders/`）。WebGL 走 GLSL 字符串、WebGPU 走 WGSL 字符串，只有 OpenGL/Vulkan 走 SPIR-V。

**`create_pipeline`（802）**：

```cpp
gl::CreateProgram(); gl::AttachShader(vs); gl::AttachShader(fs);
gl::LinkProgram(program);  // 检查 GL_LINK_STATUS，失败把 info log 报出来
return std::make_unique<OpenGLPipeline>(program, desc.wireframe, desc.depth_test,
                                        desc.depth_write, desc.blend, desc.topology, desc.instanced);
```

只 attach 顶点 + 片元两段，没有几何/计算着色器。所有 `PipelineDesc` 里的状态位**原样搬进对象**，还没应用——应用发生在 `set_pipeline`。

## 3.8 帧循环（832–902）

```cpp
Result<void> OpenGLDevice::begin_frame(SwapChain& sc) {
  return static_cast<OpenGLSwapChain&>(sc).make_current();
}
```

`OpenGLSwapChain::make_current()`（851）在 Windows 上是：

```cpp
if (hdc_) { ReleaseDC(hwnd, hdc_); hdc_ = nullptr; }   // 先还掉上次的
hdc_ = GetDC(static_cast<HWND>(window_.hwnd));          // 每帧重新取
return device_->make_current_hdc(hdc_);
```

注释里那句 `// HWND must already have a depth-capable pixel format (set on the UI thread).` 是重点：**`SetPixelFormat` 每个窗口只能调一次**，而且必须在创建窗口的那个线程（Qt UI 线程）上调。所以带深度的像素格式在 `create_opengl_surface_hwnd` 里就设好了，渲染线程只管 GetDC + MakeCurrent。

`end_frame`（897）：

```cpp
auto r = sc.present();     // SwapBuffers(hdc_) 或 glXSwapBuffers
release_current();
return r;
```

`present` 失败时 [render_runtime.cpp:1476](C:\dev\tamias\src\engine\render\runtime\render_runtime.cpp:1476) 会把 `needs_recreate = true`。

## 3.9 `OpenGLCommandList` 逐方法（904–972）

**`begin_render_pass`（904）**

```cpp
gl::Enable(GL_DEPTH_TEST);
gl::Enable(GL_SCISSOR_TEST);
gl::DepthFunc(GL_LESS);
gl::DepthMask(GL_TRUE);         // ← 关键的复位
gl::ClearColor(...); gl::ClearDepth(clear_depth);
gl::Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
```

两个细节：

1. **`DepthMask(GL_TRUE)` 是必须的**。天空管线是 `depth_write = false`，画完天空后深度写被关掉了；如果不在这里复位，之后所有模型都不写深度，Z 排序全废。这体现了 GL 的「状态是全局持久」特性——每一帧开头必须显式重建所有状态。
2. 参数里的 `SwapChain&` 被忽略了，因为 GL 直接画到 current 的默认帧缓冲；Vulkan 那边要用它拿到 image view。

**`set_pipeline`（915）** — 一次设全：

```cpp
gl::UseProgram(pipeline_->program());
gl::PolygonMode(GL_FRONT_AND_BACK, wireframe ? GL_LINE : GL_FILL);
Enable/Disable(GL_DEPTH_TEST);
gl::DepthMask(depth_write ? GL_TRUE : GL_FALSE);
if (blend) { gl::Enable(GL_BLEND); gl::BlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
else gl::Disable(GL_BLEND);
```

混合固定为预乘 alpha 的 `ONE / ONE_MINUS_SRC_ALPHA`（对应 `PipelineDesc::blend` 的注释「玻璃等半透明」）。

**`set_vertex/instance/index_buffer`（933/938/943）** — 只存指针 + 偏移，不发 GL 调用。

**`set_push_constants`（948）** — 这行最能说明「push constant 是模拟的」：

```cpp
gl::BindBuffer(GL_UNIFORM_BUFFER, device_->push_ubo());
gl::BufferSubData(GL_UNIFORM_BUFFER, 0, data.size(), data.data());
gl::BindBufferBase(GL_UNIFORM_BUFFER, kPushConstantBinding /* 0 */, device_->push_ubo());
```

`PushConstants`（[render_types.h:52](C:\dev\tamias\src\engine\render\runtime\render_types.h:52)）布局是：

```
Mat4 mvp          (64)
Mat4 model        (64)
float color[4]    (16)   rgb=基色/类别, a=opacity
float material[4] (16)   x=roughness y=metallic z=has_albedo w=has_normal
float light_dir_selected[4]
float eye_pos_mode[4]  // 相机位置 + 着色模式
float lighting[4]      // exposure / key 强度 / ibl_max_mip / has_uv
                  ──────
                  208 字节  ≤ 256 ✅
```

刚好塞进 256 字节的 UBO。**这也解释了为什么 `push_ubo_` 是 device 级共享的**：每次 draw 前覆盖同一块，GPU 端通过 UBO 读——和 Vulkan 真 push constant 的语义等价（但 Vulkan 那条路径更快，不用 `BufferSubData`）。缺点是这里**没有检查 `data.size() > 256`**。

**`set_texture`（954）**

```cpp
gl::ActiveTexture(GL_TEXTURE0 + slot);
const GLenum target = cube ? GL_TEXTURE_CUBE_MAP : GL_TEXTURE_2D;
const GLenum other  = cube ? GL_TEXTURE_2D : GL_TEXTURE_CUBE_MAP;
gl::BindTexture(other, 0);          // ← 先解绑另一个 target
gl::BindTexture(target, tex.handle());
```

**为什么要解绑另一个 target？** 因为 `GL_TEXTURE_2D` 和 `GL_TEXTURE_CUBE_MAP` 是同一个纹理单元的**不同绑定点**。如果同一个 sampler 在不同 draw 里先绑 2D 再绑 cube，残留的旧绑定可能被 sampler 读到。显式解掉 `other` 保证不会出现「一个单元上 cube 和 2D 同时有效」的未定义行为。

slot 语义在 [device.h:87](C:\dev\tamias\src\engine\render\rhi\device.h:87)：`0=albedo, 1=normal, 2=IBL irradiance, 3=IBL prefilter, 4=BRDF LUT, 5=ORM`。

**`set_viewport` / `set_scissor`（964/969）**

```cpp
void set_viewport(float x, float y, float w, float h, float, float)  // 后两个 depth range 参数被丢弃
void set_scissor(std::int32_t x, std::int32_t y, ...)
```

viewport 的 `min_depth/max_depth` 被忽略（GL 默认 depth range 就是 [0,1]，够用）。scissor 的 y 是直接透传的 —— GL 和 D3D 的 scissor 原点都在左下/左上不一致，但这里框架的约定是跟 GL 对齐的，所以直接传。

## 3.10 `draw_indexed`（974–1044）—— 真正的 draw call

这是整个文件里最值得读的函数。它把「RHI 记下的状态」翻译成一连串 GL 调用。

```cpp
if (!pipeline_ || !vertex_ || !index_) return;   // 缺一不发

gl::BindVertexArray(device_->vao());              // 全局共用一个 VAO
gl::BindBuffer(GL_ARRAY_BUFFER, vertex_->handle());
gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, index_->handle());
```

**为什么要一个全局 VAO？** Core Profile 下**必须**有 VAO 绑定才能 draw。这里用一个共享 VAO，把顶点属性**每次 draw 都重新设一遍**（`VertexAttribPointer` 是廉价的驱动状态更新），从而不用为每个 mesh 单独建 VAO。这是「正确性优先、少管资源」的取舍——代价是每次 draw 多几条 `glVertexAttribPointer`。

顶点属性 0–3 按 [mesh.h:13](C:\dev\tamias\src\engine\graphics\mesh.h:13) 的 `Vertex` 结构布局：

```cpp
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; Vec3 color{1,1,1}; };
```

| 属性 | 含义 | size | 偏移 |
|---|---|---|---|
| 0 | position | 3 | `offsetof(Vertex, position)`，加 `vertex_offset_` |
| 1 | normal | 3 | `offsetof(Vertex, normal)` |
| 2 | uv | 2 | `offsetof(Vertex, uv)` |
| 3 | color | 3 | `offsetof(Vertex, color)` |

stride 是 `sizeof(Vertex)`，全部 `divisor = 0`（逐顶点）。**注意 offset 的计算方式**：

```cpp
const auto base = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(vertex_offset_));
gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, base);
```

`set_vertex_buffer(buffer, offset)` 里的 byte offset 被**加进指针值**——这是 GL 表达「顶点缓冲子区间」的标准做法（指针参数其实不是真指针）。Vulkan 那边是 `vkCmdBindVertexBuffers(..., &offset)` 单独传。

**实例化分支（1004–1030）**：

```cpp
if (pipeline_->instanced() && instance_) {
  gl::BindBuffer(GL_ARRAY_BUFFER, instance_->handle());
  // 属性 4..9 ← GpuInstance 的 row0/row1/row2/color/material/tex_st
  gl::VertexAttribPointer(attr.index, attr.size, GL_FLOAT, GL_FALSE, inst_stride /* 96 */,
                          inst_base + attr.offset);
  gl::VertexAttribDivisor(attr.index, 1);      // ← divisor=1 = 每实例前进一条
} else {
  for (GLuint i = 4; i <= 9; ++i) { DisableVertexAttribArray(i); VertexAttribDivisor(i, 0); }
}
```

这里和 [gpu_instance.h](C:\dev\tamias\src\engine\render\runtime\gpu_instance.h) 严格对应（96 字节，`static_assert` 保证）：

| 属性 | 字段 | 用途 |
|---|---|---|
| 4 | `row0[4]` | 世界矩阵第 0 行 |
| 5 | `row1[4]` | 世界矩阵第 1 行 |
| 6 | `row2[4]` | 世界矩阵第 2 行 |
| 7 | `color[4]` | rgb 颜色 + a 不透明度 |
| 8 | `material[4]` | roughness / metallic / selected / world_scale |
| 9 | `tex_st[4]` | UV scale + offset |

用「三行 float4」而不是 float4x4，是为了 shader 里能 `dot(row, float4(pos,1))`，不用构造矩阵（HLSL 构造顺序问题）。**注意 else 分支必须 `divisor = 0` 复位**——否则 GL 会记住上次的 divisor=1，导致非实例化绘制出错。

**索引指针（1032）**：

```cpp
const auto index_ptr = reinterpret_cast<const void*>(
    static_cast<std::uintptr_t>(index_offset_ + desc.first_index * sizeof(std::uint32_t)));
```

索引用 `uint32`（对应 [mesh.h:22](C:\dev\tamias\src\engine\graphics\mesh.h:22) 的 `std::vector<std::uint32_t> indices`），所以 `first_index` 要乘 4 转成字节偏移。这跟文档里说的「找到三角面 = 索引缓冲里排好的下标」完全一致：**GPU 按每三个索引取顶点光栅化，CPU 不在 draw 时搜面**。

**拓扑与最终调用（1034–1043）**：

```cpp
const GLenum mode = pipeline_->topology() == PrimitiveTopology::LineList ? GL_LINES : GL_TRIANGLES;
const GLsizei instances = std::max(1u, desc.instance_count);
if (pipeline_->instanced())
  gl::DrawElementsInstanced(mode, desc.index_count, GL_UNSIGNED_INT, index_ptr, instances);
else
  gl::DrawElements(mode, desc.index_count, GL_UNSIGNED_INT, index_ptr);
```

注意 `LineList` 走**独立的 pipeline**（`line_pipeline_`），而不是靠线框——线框（`wireframe_`）是 `glPolygonMode(GL_LINE)`，拓扑仍是三角形、画的是三角边；`LineList` 是索引成对解释、画的是真正的线段（轴线、预览线、栅格用这个）。

## 3.11 Windows 专属的表面辅助函数（1053–1150）

这三个函数是给 **Qt 视口**用的，属于「UI 侧」而非「渲染线程侧」。

```cpp
LRESULT CALLBACK tamias_gl_surface_wnd_proc(HWND, UINT msg, WPARAM, LPARAM) {
  if (msg == WM_NCHITTEST)   return HTTRANSPARENT;   // 鼠标事件穿透到下面的 Qt 视口
  if (msg == WM_ERASEBKGND)  return 1;               // 不擦背景，避免闪烁
  if (msg == WM_PAINT)       { BeginPaint/EndPaint; return 0; }
  return DefWindowProcA(...);
}
```

**`HTTRANSPARENT` 是整个方案的关键**：GL 子 HWND 只负责出像素，鼠标完全穿透给下面的 Qt widget，所以旋转/平移/滚轮/点选全都还是 Qt 在管。窗口类注册用 `CS_OWNDC`（每个窗口私有 DC，配合每帧 GetDC/ReleaseDC）。

**`create_opengl_surface_hwnd`（1094）**：

```cpp
HWND hwnd = CreateWindowExA(WS_EX_TRANSPARENT, ..., WS_CHILD | WS_VISIBLE, ..., parent, ...);
HDC hdc = GetDC(hwnd);
PIXELFORMATDESCRIPTOR pfd{ ... 32 色 / 24 深度 / 双缓冲 ... };
const int format = ChoosePixelFormat(hdc, &pfd);
SetPixelFormat(hdc, format, &pfd);       // ← 只能设一次，所以必须在 UI 线程搞定
ReleaseDC(hwnd, hdc);
log_info("OpenGL surface HWND created on UI thread (depth-capable)");
```

调用点在 [document_viewport.cpp:569](C:\dev\tamias\src\app\viewport\canvas\document_viewport.cpp:569) 的 `ensure_gl_surface()`：

```cpp
if (AppSettings::instance().graphics_backend() != GraphicsBackend::OpenGL) return;
const WId parent_id = surface_->winId();              // 逼 Qt 先造出 native child
gl_hwnd_ = create_opengl_surface_hwnd(reinterpret_cast<void*>(parent_id), w, h);
```

`opengl_backend.h` 里那句注释说明了为什么必须这样：

> `// Creating HWNDs on the render thread deadlocks with Qt's message loop.`

`resize_opengl_surface_hwnd`（1138）先 `GetClientRect` 比对，尺寸相同就跳过（避免无谓的 `SetWindowPos`），否则 `SetWindowPos(SWP_NOZORDER | SWP_NOACTIVATE)`。它每帧在 `submit_current_frame` 里被调用。

`destroy_opengl_surface_hwnd`（1132）就是 `DestroyWindow`。

## 3.12 注册（1154–1157）

```cpp
void register_opengl_backend() {
  log_info("Registering OpenGL RHI backend");
  register_backend(BackendModule{GraphicsBackend::OpenGL, create_opengl_device});
}
```

`register_backend` 在 [device_factory.cpp:20](C:\dev\tamias\src\engine\render\rhi\device_factory.cpp:20) 往一张全局表里塞工厂函数。之后 `RHIDevice::create(info)` 按 `GraphicsBackend` 查表并调用 `create_opengl_device`（1046），后者 `make_unique<OpenGLDevice>` + `initialize()`，失败就把错误往上抛。入口由 [rhi_backends.cpp](C:\dev\tamias\src\app\base\rhi_backends.cpp) 的 `register_linked_rhi_backends()` 在 `main()` 早期调用，受 `TAMIAS_HAS_RHI_OPENGL` 编译开关控制。

---

# 四、上下文联动：一帧的完整数据流

把上面的碎片串起来，一次 OpenGL 帧的完整路径是：

```
[UI 线程]
  DocumentViewport::ensure_gl_surface()
    └─ create_opengl_surface_hwnd()  → 子 HWND + 24 位深度像素格式
  DocumentViewport::submit_current_frame()
    ├─ resize_opengl_surface_hwnd(hwnd, w, h)
    ├─ channel_->resize(native_handle(), w, h)      // native_handle() 返回 gl_hwnd_
    └─ channel_->submit(FrameSubmission)             // 跨线程丢给 RenderThread

[渲染线程]
  RenderThread::draw_channel()
    ├─ device_->create_swap_chain(desc)              → OpenGLSwapChain（只存 handle）
    ├─ device_->begin_frame(swap_chain)
    │     └─ OpenGLSwapChain::make_current()
    │           ├─ GetDC(hwnd)
    │           └─ device_->make_current_hdc(hdc) → wglMakeCurrent(hdc, context_)
    ├─ command_list->begin()
    ├─ command_list->begin_render_pass(sc, clear_color, 1.f)
    │     └─ glClear(COLOR | DEPTH)   ← 立即执行
    ├─ set_viewport / set_scissor
    ├─ 对每个 SceneDrawItem：
    │     ├─ set_pipeline(program)        → glUseProgram + 状态位
    │     ├─ set_texture(slot 0..5)       → glActiveTexture + glBindTexture
    │     ├─ set_push_constants(PushConstants) → glBufferSubData(UBO 0) + glBindBufferBase
    │     ├─ set_vertex_buffer / set_index_buffer   （只记录）
    │     └─ draw_indexed(desc)           → VAO + 属性 + glDrawElements(Instanced)
    ├─ command_list->end()
    ├─ device_->execute(cmd)              → 空操作
    └─ device_->end_frame(sc)
          ├─ SwapBuffers(hdc_)
          └─ release_current()
```

**资源创建的独立通路**（不在这条帧循环里，可能在渲染线程的任何时刻发生）：

```
RenderThread::upload_mesh(mesh_id, MeshCpu)
  └─ device_->create_buffer({size, Vertex, host_visible=true})  → glGenBuffers
     device_->create_buffer({size, Index,  host_visible=true})  → glGenBuffers
         └─ buffer->write(0, bytes)  → BindBuffer + BufferSubData
                                   （内部 make_current_dummy / release_current）

resync_textures()
  └─ device_->create_texture(desc)                  → glGenTextures
     texture->write_subresource(mip, layer, data)   → glTexImage2D（分配 + 上传一起）
```

**和 world 层的关键接缝**：

| 接缝 | 位置 | 内容 |
|---|---|---|
| 三角从哪来 | `MeshCpu` → `create_buffer` | VBO/IBO 里已经排好的顶点和索引 |
| 画谁 | `SceneDrawItem` → `draw_indexed` | 索引计数 + 偏移 |
| 世界变换 | `GpuInstance.row0/1/2` → 属性 4/5/6 | 每实例一行 float4 |
| 相机 | `push_constants.mvp` | 含 `clip_space_correction_matrix()` |
| 颜色/材质 | `push_constants` + `set_texture` | slot 0–5 |

---

# 五、值得注意的点与潜在坑

按「会不会真出问题」排序：

**1. 没有启用 sRGB 帧缓冲（最值得核对的一项）**

Vulkan 的 swapchain 用 `VK_FORMAT_B8G8R8A8_SRGB`（[vulkan_device.cpp:504](C:\dev\tamias\src\engine\render\rhi\vulkan\vulkan_device.cpp:504)），硬件在写入时自动做 linear→sRGB 编码。OpenGL 这边像素格式是用 `ChoosePixelFormat(pfd)` 选的、**没有指定 `WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB`**，文件里也**没有任何 `glEnable(GL_FRAMEBUFFER_SRGB)`**。albedo 纹理走 `GL_SRGB8_ALPHA8`（采样时解码到线性），着色在线性空间算，但结果直接写进一个非 sRGB 缓冲——严格来说会和 Vulkan 路径有 gamma 差异（画面偏暗）。如果两边同场景截图对比发现亮度不一致，这里是第一嫌疑。

**2. `gl_texture_formats` 的 `B8G8R8A8_SRGB` 落到默认分支**

```cpp
default: internal = GL_SRGB8_ALPHA8; format = GL_RGBA; type = GL_UNSIGNED_BYTE;
```

`B8G8R8A8` / `D32_SFLOAT` 都掉进 default。**没有处理 BGRA 的通道顺序**——如果哪天真上传 BGRA 字节，会被当 RGBA 解释导致红蓝互换。目前 `render_runtime` 上传只用 `R8G8B8A8_SRGB` / `R8G8B8A8_UNORM` / `R16G16B16A16_SFLOAT` / `R16G16_SFLOAT`（[render_runtime.cpp:479](C:\dev\tamias\src\engine\render\runtime\render_runtime.cpp:479) 起），所以暂时没暴露。

**3. `set_push_constants` 不检查大小**

UBO 固定 256 字节，但 `BufferSubData` 直接传 `data.size()`。目前 `sizeof(PushConstants) == 208`，安全。将来往 `PushConstants` 加字段要盯住这个 256 的上限（Vulkan 的 push constant 上限通常是 128，所以那边可能更早爆）。

**4. `OpenGLDevice::make_current_window`（Win32 版，529）是死代码**

Win32 分支里 `OpenGLSwapChain::make_current` 自己 GetDC + `make_current_hdc`，从不调它。GLX 分支里同名函数是活的（682）。清理时别误删 GLX 那份。

**5. `OpenGLFence::wait()` 是 `glFinish()`**

全流水线停顿，非常粗。`reset()` 空实现也是暗示「这个 fence 不能重复用」。如果将来要做 CPU/GPU 并行，这里得换成 `GL_ARB_sync` 的 `glFenceSync` / `glClientWaitSync`。

**6. `DrawIndexedDesc::vertex_offset` / `first_instance` 被忽略**

Vulkan 走 `vkCmdDrawIndexed(..., desc.vertex_offset, desc.first_instance)`（[vulkan_device.cpp:357](C:\dev\tamias\src\engine\render\rhi\vulkan\vulkan_device.cpp:357)），GL 这边只用了 `first_index` 和 `instance_count`。因为 `render_runtime` 从来没设过那两个字段，所以现在行为一致；但这是**接口语义上的一处隐式不对称**，将来用 baseVertex 做 mesh 合批时会踩。

**7. 没有 GL 调试输出**

`DeviceCreateInfo::enable_validation` 在这个文件里完全没被读到——GL 侧没有 `GL_KHR_debug` / debug callback，出错只能靠 `glGetError`（而且只有 `create_shader_module` 里检查了一次）。Vulkan 那边有 validation layer，两边的可调试性不对等。

**8. 每次 draw 都重设全部顶点属性**

共享一个 VAO + 每次 `glVertexAttribPointer` 是「少管资源」的取舍。如果某个场景 draw call 数很高，这部分会成为 CPU 热点（每 draw 约 10~16 次属性调用）。真要优化就是给每个 `GpuMesh` 配一个 VAO。

---

一句话总结：**这个文件是 Tamias 的「OpenGL 方言字典」**——它把 `device.h` 定义的十几个后端无关动词，翻译成 GL 的立即模式调用，并把三种硬约束（GL context 的线程局部性、Core Profile 必须绑 VAO、Windows 必须先有 context 才能取 WGL 扩展）全部吸收在自己内部，让上层 `render_runtime.cpp` 的绘制剧本一行都不用改。