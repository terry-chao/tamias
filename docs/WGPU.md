# wgpu 怎么接到 Tamias 的 RHI 上

> **状态：方案已定，代码未落地。** wgpu **不是场景图，也不自己找三角面**。它应成为 RHI 的第三块翻译官：渲染线程已经决定画哪份网格、用哪张贴图，wgpu 只负责把同一套动词变成 WebGPU 调用并 present。一帧剧本见 [管线与 RHI](RENDERING.md)；OpenGL 对照见 [OpenGL 后端](OPENGL.md)；屏外不画见 [视锥剔除](FRUSTUM-CULLING.md)。

抽象接口：[device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)。落地目录预定 `src/engine/render/rhi/wgpu/`（尚未创建）。

---

## 0. 为什么走这条路

Tamias 已经有自研 RHI：`draw_channel` 不认 Vulkan / OpenGL，只认 `RHIDevice` 的动词。wgpu（通过 **wgpu-native** 的 C ABI）是 WebGPU 的一种桌面实现，内部再选 DX12 / Vulkan / Metal。

正确接入点是：**像 OpenGL 一样，再做一个后端实现**。不要改绘制剧本，也不要用 wgpu 替换整层 RHI 抽象。

| 方案 | 做法 | 结论 |
|---|---|---|
| **A. 第三 RHI 后端** | `rhi/wgpu` 实现现有 `device.h` | **采用。** `draw_channel` / 场景 / OCCT 不动 |
| B. 用 wgpu 换掉自研 RHI | 绘制代码直接写 WebGPU | 否。bind group、WGSL、无 polygon mode 会掀开绘制层 |
| C. 用 wgpu 替换手写 Vulkan | 默认走 wgpu，OpenGL 仍做老卡兼容 | **P1 对齐之后的产品决策**，不当第一步 |

一期目标是 **桌面 Qt 视口**，不是浏览器。wasm 没有 Qt Widgets，那是另一条产品线。

实现库：**wgpu-native**（`webgpu.h`，发预编译 `wgpu_native.dll` / `.so`），仓库不引 Rust 工具链。Dawn 更「正统 C++」但更重；若以后要跟 Chromium 对齐再换，RHI 边界可以保住。

---

## 1. 它在整条链的哪一层

```
OCCT / OBJ  → MeshCpu（CPU 上的顶点+索引）
Document     → SceneDrawItem（谁、世界矩阵、材质 id）
视口         → FrameSubmission 交给 RenderThread
RenderThread → 只认 RHI 动词：create_buffer / set_texture / draw_indexed
wgpu 后端    → 把这些动词变成 WebGPU：encoder / bind group / queue / present
```

登记路径已经为第三后端预留：`main()` → `register_linked_rhi_backends()` → `RHIDevice::create()` 按设置里的 `GraphicsBackend` 选一个。OpenGL 就是按这套加进去的。

| 层 | 职责 | wgpu 该不该碰 |
|---|---|---|
| `Scene` / `render_items()` | 展平 draw list | 否 |
| `DocumentViewport` | 相机、点选、提交帧 | 只加窗口 / 设置分支 |
| `RenderThread::draw_channel` | 一帧剧本 | **否** |
| `RHIDevice` | 建缓冲 / 管线 / draw / present | **新增一个实现** |
| `GraphicsBackend` | 现在 Vulkan / OpenGL | 加 `Wgpu` |

**不要把 bind group / WGSL 抬到 `device.h`。** 路线图里真正该扩的是 texture / UBO / sampler；wgpu 的 bind group 藏在后端里，和 OpenGL 把 push constant 映射成 UBO 一样。

---

## 2. 设备、线程、窗口

wgpu 是命令录制模型，线程模型接近 Vulkan，不是 OpenGL。

**线程**

- `shares_execution_thread_with`：**按 Vulkan 处理**（同 backend + 同校验 → 多视口共一条 `RenderThread`）。
- Device / Queue / Surface 只在渲染线程碰。
- `enable_validation` → wgpu 的 log / validation 回调，不另开一套。

**窗口**

- Win32：和 Vulkan 一样，用 Qt 子控件的 `HWND`（`surface_->winId()`），**不要**走 OpenGL 那套额外 GL child HWND。
- Linux：已有 `NativeWindowHandle.display` + `window`，对应 Xlib surface。
- `begin_frame` → 取当前 surface 纹理
- `end_frame` → present
- `resize` → 重新 configure surface（等价现在的 swapchain recreate）

颜色格式按表面能力选；Windows 上通常是 `BGRA8UnormSrgb`，和现在 Vulkan 一致。

---

## 3. Clip / NDC

WebGPU 与 Vulkan 相同：**Y 向下，Z ∈ [0, 1]**。

`clip_space_correction_matrix()` 直接复用 Vulkan 那份（翻 Y）。世界空间视锥、`proj * view` 提平面的规则不变——**不要**把 clip 校正乘进剔除。详见 [视锥、NDC 与屏幕](NDC.md)。

---

## 4. Shader 与资源绑定

当前两套产物：

| 文件 | 给谁 | 常量怎么进 shader |
|---|---|---|
| `*.spv` | Vulkan | `[[vk::push_constant]]` |
| `*.gl.spv` | OpenGL | `ConstantBuffer` `register(b0)` |

wgpu 的坑：

- 标准 WebGPU 着色器是 **WGSL**，没有 spec 级 push constant。
- Tamias 的 `PushConstants` 约 **192 字节**，不少设备 push constant 上限是 128。
- 线框现在靠 `PipelineDesc.wireframe` → polygon mode；**浏览器 WebGPU 没有这个**。桌面 wgpu 有 native feature `POLYGON_MODE_LINE`。

**一期做法：**

1. 常量走 **uniform buffer**（和 GL 一样），`set_push_constants` 在 wgpu 后端写成动态 UBO。
2. 着色器先吃 SPIR-V：新建 DXC 变体 `*.wgpu.spv`（或确认 naga 能稳定吃 `*.gl.spv`），`register(b0/t0/s0)` 对齐。
3. `ShaderLanguage` 以后再加 `Wgsl`；手写 WGSL 放到二期。
4. 线框一期开 **wgpu native `POLYGON_MODE_LINE`**（桌面可接受）。若以后要 web，再改成重心坐标 discard 或边线 IBO。

固定 bind group（所有管线同一套，**后端内部创建**，不必改 `PipelineDesc`）：

| group 0 | 资源 | 对应现在 |
|---|---|---|
| binding 0 | uniform `PushConstants` | `set_push_constants` |
| binding 1 | texture 2D | `set_texture(slot 0)` |
| binding 2 | sampler | 后端固定线性 / repeat |

顶点布局继续写死在后端：`Vertex` 的 location 0–3（pos / normal / uv / color），与 Vulkan / GL 一致。无贴图时绑 1×1 白纹理，避免空绑。

---

## 5. 命令模型

按 Vulkan 录制，不要做成 GL 那种「每句立刻 gl*」：

```
begin_frame(surface)
  cmd.begin()
  cmd.begin_render_pass(...)   // color + depth32
  set_pipeline / set_* / draw_indexed
  cmd.end_render_pass()
  cmd.end()
execute(cmd)                   // submit queue
end_frame                      // present
```

深度纹理、默认白纹理、天空 / 网格 / 线管线，都在现有 `ensure_pipelines` 里建；wgpu 只负责把 `create_*` 填实。`draw_channel` 一行都不用为 wgpu 改。

---

## 6. 要动哪些文件（尽量小）

```
src/engine/graphics/graphics_backend.h     + Wgpu
src/engine/render/rhi/CMakeLists.txt       + add_subdirectory(wgpu)
src/engine/render/rhi/wgpu/                新后端（对外仍是 device.h）
  wgpu_backend.h
  wgpu_device.cpp
  CMakeLists.txt
src/app/rhi_backends.cpp                   register_wgpu_backend()
src/app/app_settings.* / settings_dialog   设置项
src/engine/render/render_runtime.cpp       选 shader 变体（opengl / wgpu / vulkan）
cmake/TamiasShaders.cmake                  可选 *.wgpu.spv
CMakeLists.txt                             TAMIAS_ENABLE_WGPU_BACKEND（默认 OFF）
```

`draw_channel`、`document.cpp`、OCCT、点选 **不改**。视口只在设置枚举和「要不要建 GL child HWND」上认新 backend（wgpu 走 Vulkan 那条 HWND 路径）。

依赖：FetchContent / 预编译 wgpu-native，**不要塞进 vcpkg.json 当必选**（Windows 现在连 Vulkan 都走系统 SDK）。POST_BUILD 把 `wgpu_native.dll` 拷到 `tamias.exe` 旁，和 OCCT / Qt 一样。

---

## 7. 分期

**P0 — 能亮一帧**

1. CMake option + 链上 wgpu-native。
2. `WgpuDevice`：instance / adapter / device / queue。
3. HWND / X11 surface + configure + present 清屏色。
4. 设置里出现 WebGPU，重启后生效（与现有后端切换方式一致）。

**P1 — 视口功能对齐**

5. buffer / texture / sampler / pipeline / indexed draw。
6. push constants → 动态 UBO；贴图 bind group；默认白纹理。
7. clip 矩阵 = Vulkan；shader 用 UBO 变体。
8. 线框用 `POLYGON_MODE_LINE`。
9. 多视口共线程；resize / mailbox 帧提交沿用 `RenderChannel`。

**P2 — 产品化**

10. 校验层、设备丢失、DPI resize。
11. 再决定：Windows 默认仍 Vulkan，还是改成 wgpu（拿 DX12 / Metal）。

P0 做完再谈替不替 Vulkan。

---

## 8. 明确不做

- 不把 bind group / WGSL 抬到 `device.h`（那会逼所有后端改）。
- 一期不上 wasm / 浏览器。
- 不删 Vulkan、不让 wgpu 自带的 GL 后端替代现有 OpenGL 4.5 路径（老驱动兼容是现在这条 GL 的存在理由）。
- 不为 wgpu 单独做拾取 / 离屏（拾取仍是 CPU BVH）。

---

## 9. 风险（提前认）

1. **线框**：标准 WebGPU 无 polygon mode；桌面靠 native feature，web 要另做。
2. **SPIR-V → naga**：带 `vk::push_constant` 的 Vulkan SPIR-V 很容易失败，所以必须走 UBO 变体。
3. **ABI**：wgpu-native 要钉版本，随 exe 发布，不能指望系统已装。
4. **CAD 后期功能**（几何着色器、clip distance 剖切）：WebGPU 没有或很弱；截面应继续用 shader `discard` / stencil，这和 [路线图](ROADMAP.md) 一致。

---

## 10. 一句话对照

| 问题 | 答案 |
|---|---|
| wgpu 在 Tamias 里是什么 | RHI 第三后端（计划），桌面 WebGPU |
| 谁给它三角 | 和 Vulkan / GL 一样：`MeshCpu` 上传成 GPU 缓冲 |
| 常量怎么传 | `set_push_constants` → 动态 UBO，不走 spec push constant |
| 线框怎么画 | 一期 native `POLYGON_MODE_LINE` |
| 窗口怎么绑 | 与 Vulkan 相同的 Qt HWND / X11，不建 GL child |
| 它不管什么 | 特征树、OCCT、点选、视锥剔除、合批、`draw_channel` |

换 wgpu 时，上面 `draw_channel` 一行都不用改；变的是新目录里那些 WebGPU 调用。wgpu 在项目里就是「同一套绘制剧本的第三种方言」。
