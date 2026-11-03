# 渲染线程与 RenderChannel 调度

> 一个 RHI Device 能服务多个视口，但只能被**一条线程**拥有。Vulkan 把这句话实现成「一 Device + N Channel」；OpenGL 把这句话守成「一条线程 + 一个 context + N 个 drawable」，跨线程一律不共用。这一页讲 `RenderThread` / `RenderChannel` / `RenderThreadPool` 怎么把两种后端塞进同一套调度，以及 OpenGL 为什么整体退出共用。

实现：[render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h) / [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp)；OpenGL 侧 [opengl_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp)；视口侧 [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/canvas/document_viewport.cpp)。一帧怎么画见 [管线与 RHI](RENDERING.md)，OpenGL 的缓冲 / 贴图 / 呈现见 [OpenGL 后端](OPENGL.md)。

---

## 0. 两层不变式

整套调度只有两条不变式：

1. **Device 线程亲和**。`RHIDevice` 只在「创建它、并且一直用它」的那条线程上被调用。资源创建、命令录制、呈现都算。
2. **Channel 是表面单位，不是所有权单位**。`RenderChannel` 只是「某条线程上的一个表面 + 一帧 + 一份留存状态」，它不拥有 Device，也不能换线程。

于是：

- **thread** = 所有权单位：一条线程、一个 Device、一套管线与资源缓存；
- **channel** = 表面单位：一个视口、一块 swapchain、一份 mailbox 最新帧、一份留存场景图。

```
RenderThreadPool
├─ RT#1  Vulkan, validation=on    ← Device + queue
│    ├─ channel 文档 A
│    ├─ channel 文档 B
│    └─ channel 场景调试器
├─ RT#2  Vulkan, validation=off   ← 另一台 Device
└─ RT#3  OpenGL                   ← OpenGLDevice + GL 4.5 context（只有一个视口）
```

---

## 1. 调度契约

### 1.1 配置就是兼容性判据

```cpp
struct RenderDeviceConfig {
  GraphicsBackend backend = GraphicsBackend::Vulkan;
  bool enable_validation = true;
  bool synchronous = false;   // true：不起 worker，宿主线程 pump()（wasm / WebGL）

  bool shares_execution_thread_with(const RenderDeviceConfig& other) const {
    return backend != GraphicsBackend::OpenGL && backend != GraphicsBackend::WebGL &&
           backend != GraphicsBackend::WebGPU && !synchronous && !other.synchronous &&
           backend == other.backend && enable_validation == other.enable_validation;
  }
};
```

判据只有三项：**后端相同、校验设置相同、双方都异步**。OpenGL / WebGL / WebGPU 由后端直接短路，`synchronous` 再兜一层——浏览器线跑在宿主线程上，本来就没有可复用的 worker。

一个容易忽略的细节：`enable_validation` 是判据的一部分，但 OpenGL 侧**从不读它**（没有 `KHR_debug` 回调）。这个 key 是给 Vulkan 的 validation layer 设计的。

### 1.2 acquire：线性查找，找不到就新建

```cpp
std::shared_ptr<RenderThread> RenderThreadPool::acquire(const RenderDeviceConfig& config) {
  for (auto& thread : threads_)
    if (thread->config().shares_execution_thread_with(config)) return thread;
  auto thread = std::make_shared<RenderThread>(config);
  thread->start();                       // 这里才 RHIDevice::create(...)
  threads_.push_back(thread);
  return thread;
}
```

| 配置 | `acquire` 的结果 |
|---|---|
| Vulkan + 相同 `enable_validation` | 复用已有线程与 Device，只新建 channel |
| Vulkan + 不同 `enable_validation` | 新线程 + 新 Device（校验层是设备级状态） |
| OpenGL | **每次**新线程 + 新 Device + 新 GL context |
| `synchronous`（wasm / WebGL） | 新 `RenderThread` 对象，但不 spawn 线程，宿主线程 `pump()` |

### 1.3 视口只做两件事

```cpp
// DocumentViewport::ensure_channel()
if (!render_thread_) render_thread_ = RenderThreadPool::instance().acquire(config);
channel_ = std::make_unique<RenderChannel>(render_thread_, render_thread_->create_channel());
```

`RenderChannel` 因此薄到只有两个成员：`shared_ptr<RenderThread>` + `channel_id`。`submit()` / `resize()` 都只是把参数转给线程；析构时 `destroy_channel()` 把删除动**投递回渲染线程**再等完成——GL 对象必须带着 current context 删，而且不能和 `draw_channel()` 里对 `channels_` 的引用并发。

---

## 2. OpenGL 为什么整条线程退出共用

不是「GL 慢」，是四条硬约束，每条都能在代码里指出位置。

### (a) context 是线程局部的，同一时刻只能 current 在一个线程

`wglMakeCurrent(hdc, context)` / `glXMakeCurrent(dpy, drawable, context)` 建立的是「(线程, drawable) → context」的绑定。一个 context 不能同时被两条线程 current；一个线程同时也只能有一个 current context。

### (b) 没有 share group，句柄离开 context 就无效

`RenderThread` 的 `meshes_`、`textures_`、`pipelines_`、全局 VAO、push-constant UBO 都是 **Device 级**缓存，里面存的是 `OpenGLDevice` 在自己 context 上 `glGen*` 出来的 GLuint。如果让两个视口共用一个 `RenderThread` 却各持一个 context，这些名字在另一个 context 里根本不存在。

当前代码里**没有任何** `wglShareLists` / `glXCreateContext(share)` 调用——共享组这条路根本没开。

### (c) 状态机是 context 全局的，而 GL 后端是立即模式

`OpenGLDevice::execute()` 是空的：每句 `set_*` / `draw_indexed` 当场变成 GL 调用。`draw_channel()` 因此依赖「一帧开头把状态全部重建」：`begin_render_pass()` 复位 `DepthMask`，`set_pipeline()` 重设 polygon mode / blend / depth，`draw_indexed()` 重设全部顶点属性与 divisor。

这套写法只在**单线程串行**下成立。两条线程同时往一个 context 发调用会互踩状态；而且 `wglMakeCurrent` 每次都要拿驱动的全局锁，并发只会退化成串行 + 卡顿。

### (d) `current_` 本身就不是线程安全的

`OpenGLDevice` 用普通成员 `CurrentTarget current_` + `current_hdc_` 记录「现在挂在哪个 drawable 上」，`ensure_current()` 无锁读取这个状态。这行代码就是「单线程独占」的自证。

> 换句话说：`RenderThread` 的设计前提是「Device 被这条线程独占」；OpenGL 的 context 恰好也只能被一个线程 current。两者是同一条约束的两种说法，所以 GL 的「隔离」不是额外加锁，而是**在调度层直接不共用**。

---

## 3. 一条 GL 线程内部：channel 复用同一个 context

排除共用之后，GL 侧的「复用」发生在下面这一层：

```
RenderThread (OpenGL)
├─ 设备级：pipelines、VAO、push UBO、meshes_、textures_、默认贴图、IBL
└─ RenderChannel
     ├─ swap_chain        ← 子 HWND（或离屏 FBO）的 drawable
     ├─ command_list      ← 只记指针 / 偏移，draw 时才发 GL 调用
     ├─ instance_buffer   ← 每通道一块，双槽 ping-pong
     ├─ latest            ← mailbox 最新帧
     └─ scene_root        ← 留存场景图 + lod_by_node
```

- **切 channel = 换 drawable，不换 context**：`begin_frame()` 只是 `GetDC()` + `wglMakeCurrent(hdc, context_)`，所以设备级对象（program、VAO、UBO、VBO/IBO、纹理）在通道之间一直有效。
- **实例缓冲必须每通道一块**：共用一个 host-visible buffer 时，后画的通道会盖掉前一个通道仍在 GPU 上的 AABB / instance 数据，黄线会隔帧闪。
- **结构上一条 GL 线程可以挂多个 channel**（`draw_channel()` 只碰 `ChannelState` 与设备级资源），但 pool 不会把同一条 GL 线程发给第二个视口——实际就是 1 文档 = 1 线程 = 1 context。

---

## 4. 一帧的时序（OpenGL）

```
UI 线程                                  渲染线程
ensure_gl_surface()                      thread_main():
  create_opengl_surface_hwnd()             cv.wait(有任务，或某 channel 有 latest)
  （SetPixelFormat 只能在此线程做一次）     → 先跑任务（上传 / 销毁 channel / 离屏 resize）
submit_frame() → latest = frame            → 对每个有 latest 的 channel: draw_channel()
resize_surface() → needs_recreate             begin_frame  → GetDC + wglMakeCurrent(窗口)
                                              glClear / 各 draw（立即执行）
                                              execute()    → 空操作
                                              end_frame    → SwapBuffers + release_current
```

四个关键点：

1. **mailbox 语义**：`submit_frame()` 只覆盖 `latest`，UI 永远不等渲染线程；来不及画的中间帧直接丢。
2. **设备创建在调用线程上**：`RenderThread::start()` 先在 UI 线程 `RHIDevice::create()`，`create_dummy_context()` 建 context / UBO / VAO，然后 `release_current()` 才 spawn worker。GL context 没有线程亲和性，只有「同时最多一个 current 线程」，所以这次交接是安全的。
3. **只在帧边界切 drawable**：`begin_frame` / `end_frame` 是唯一切换点；资源创建（上传 mesh / 贴图、删对象）走 `ensure_current()`——已经 current 就一次驱动调用都不发。早期「每个 GL 操作切 dummy 再解绑」的写法会把帧内窗口上下文踢掉，导致 draw 静默失效（复盘见 [RHI 卡死 / 崩溃定位](RHI-TRIAGE.md)）。
4. **任务先于绘制**：`thread_main()` 先跑队列里的任务，再采样脏 channel，保证 `destroy_channel()` 不会和 `draw_channel()` 抢同一个 `ChannelState`。

> 一条线程一轮把所有脏 channel 各画一帧，没有优先级、没有时间片。Vulkan 共线程时一个重视口会拖慢其他视口；OpenGL 因为一线程一视口，反而不存在这个问题。

---

## 5. Vulkan / OpenGL 对照

| 维度 | Vulkan | OpenGL |
|---|---|---|
| 复用粒度 | 同后端 + 同校验 → 共线程 / 共 Device | 从不共用 |
| 每视口成本 | 一个 channel（swapchain + command list） | 一条线程 + 一个 context + 一整套管线 / VAO / UBO |
| 跨视口共享 GPU 资源 | 是（`meshes_` / `textures_` 同一份） | 否（每个 context 各自一份） |
| 通道切换 | `vkCmd*` 录制 + 一次 `vkQueueSubmit` | `wglMakeCurrent` 换 drawable（要驱动全局锁） |
| `execute()` | 真正的提交 | 空操作（立即模式） |
| 配置判据 | 后端 / 校验 / 异步 | 恒 `false` |

---

## 6. 如果真要让 OpenGL 视口也复用

三条路，代价递增：

**A. 一条线程 + 每视口一个 context（复用执行线程，隔离 context）**
线程在帧边界 `make_current(channel)` 切换 context。但每个 context 要各自一套资源（或共享组）；Device 的 `current_` 要变成「每 context 状态」，`ensure_current()` 的幂等缓存也要按 context 分表。收益只是少几条线程，提交照样被驱动锁串行化。

**B. share group（`wglShareLists` / `glXCreateContext(share)`）**
buffer / texture / shader / program / sync 对象可跨 context 共享；**VAO、FBO、query、默认帧缓冲不共享**，仍按 context 一份。共享关系在创建 context 时就定死，而 `create_dummy_context()` 是在 `acquire()` 时执行的，那时还不知道后面会开哪些视口。还要处理「同一份 pipeline 在两个 context 里各有一份 program」的映射。

**C. 一条线程 + 一个 context + N 个 drawable**
就是现在「线程内多 channel」的形态，把多个 GL 视口都挂到一个线程上。实现最省（`draw_channel()` 已经支持），但所有 GL 视口严格串行，一个重场景会拖住其他视口。

选「不共用」的本质是：**GL 的复用收益（省几条线程）远小于正确性成本（上下文 / 状态 / 句柄三重隔离）**；而 Vulkan 天然支持「一 Device 多线程录制 + 每通道独立 swapchain」，只有它配得上这套调度。

---

## 7. 现状代价

读代码时容易忽略的几点：

1. **线程 / 上下文只增不减**：`RenderThreadPool::threads_` 只在 `shutdown()` 清空，关掉的文档页签不会归还线程。GL 下每开一个文档 = 一条线程 + 一个 context 常驻到退出。
2. **每个 GL 视口重复编译管线**：`ensure_pipelines()` 是 per-`RenderThread` 的，SPIR-V `glSpecializeShader` + link 全套要再来一遍。
3. **场景调试器在 GL 下重复上传**：`ReplayViewport` 在自己的线程上 `request_upload_mesh()`（[replay_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/replay/replay_viewport.cpp)）；GL 拿不到主文档的线程，只能把快照里的网格再传一份。
4. **UI 文案已经把这套语义写死**：设置页对 OpenGL 显示「isolated render thread per document (never shared)」（[settings_dialog.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/shell/dialog/settings_dialog.cpp)）。

---

## 延伸阅读

- [管线与 RHI](RENDERING.md)：`FrameSubmission`、`draw_channel` 的绘制顺序
- [OpenGL 后端](OPENGL.md) §6：`ensure_current` / 帧边界切换的上下文管理
- [RHI 卡死 / 崩溃定位](RHI-TRIAGE.md)：一次真实驱动现场的复盘
- [UE5 对照](UE5-VS-TAMIAS.md) / [Qt3D 对照](QT3D-VS-TAMIAS.md)：另一套线程模型长什么样
