# 浏览器 WebGPU：WASM 查看器的 RHI

> **状态：浏览器后端已落地。** 桌面 **不做** wgpu-native。WebGPU 只出现在引擎 WASM 查看器里，和桌面 Vulkan / OpenGL 并列，都实现同一份 [`device.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)。一帧剧本见 [管线与 RHI](RENDERING.md)；浏览器产品线见 [Web 查看器](WEB.md)。

---

## 0. 为什么不是桌面 WebGPU

自研 RHI 已经把 `draw_channel` 从 Vulkan / OpenGL 里隔开。再在 Windows/Linux 上接 **wgpu-native**，多半还是：

```
draw_channel → RHI → wgpu-native → Vulkan（或 DX12）
```

同一套动词翻两遍，底下仍是已经会说的 API；还要钉 ABI、随包发 DLL。macOS / Metal 目前不是产品目标。

浏览器要 WebGPU，走的是 **Emscripten + `navigator.gpu`**，加载不了 `wgpu_native.dll`。所以：

| 产品线 | 后端 |
|---|---|
| 桌面 Qt | Vulkan 主、OpenGL 兼容老卡 |
| 浏览器 | **WebGPU**（`rhi/webgpu`）；WebGL2 仍可作可选回退 |

---

## 1. 它在整条链的哪一层

```
Web UI (web/)
  → ViewerHost
    → RenderThread（synchronous=true）
      → RHIDevice 动词
        → rhi/webgpu  （emdawnwebgpu → 浏览器 WebGPU）
```

`draw_channel`、场景、OCCT **不改**。bind group / WGSL 不抬到 `device.h`。

| 层 | 职责 | WebGPU 该不该碰 |
|---|---|---|
| `Scene` / `render_items()` | 展平 draw list | 否 |
| `ViewerHost` | 相机、文件、提交帧 | 只登记 backend |
| `RenderThread::draw_channel` | 一帧剧本 | **否** |
| `RHIDevice` | 建缓冲 / 管线 / draw / present | **本目录实现** |

---

## 2. 实现要点

- **库：** Emscripten 的 `--use-port=emdawnwebgpu`（Dawn 维护的 `webgpu.h`，跑在浏览器 WebGPU 上）。CMake：`TAMIAS_ENABLE_WEBGPU_BACKEND`（WASM 预设强制 ON）。
- **异步：** `RequestAdapter` / `RequestDevice` 用 `TimedWaitAny`；`tamias_viewer` 链 `-sASYNCIFY=1`，让 `ViewerHost::start()` 仍是同步的。
- **窗口：** `NativeWindowHandle.canvas_selector`（`#viewport`）→ `WGPUEmscriptenSurfaceSourceCanvasHTMLSelector`。
- **线程：** 与 WebGL 相同，`synchronous=true`，不跟其它视口共线程。
- **Clip / NDC：** 与 Vulkan 相同（Y 向下，Z ∈ [0, 1]），`clip_space_correction_matrix()` 翻 Y。
- **常量：** `set_push_constants` → 动态 UBO（256 字节对齐环）。标准 WebGPU 没有 push constant。
- **着色器：** 内嵌 WGSL（`webgpu_shaders.h`），不走 DXC / SPIR-V。浏览器不收 SPIR-V。
- **线框：** 标准 WebGPU 无 polygon mode；与现 WebGL 一样，线框模式先走片元里的 `mode` 分支（实体着色）。以后要真线框再做重心 discard 或边线 IBO。
- **贴图：** 固定 bind group 0：UBO + albedo / normal / IBL cube / BRDF LUT + sampler。无贴图绑 1×1 白纹理。

命令模型按 Vulkan 录制：`begin_frame` 取 surface 纹理 → encoder / render pass → `queue.submit` → present。

---

## 3. 文件

```
src/engine/graphics/graphics_backend.h     GraphicsBackend::WebGPU
src/engine/render/rhi/webgpu/
  webgpu_backend.h
  webgpu_device.cpp
  webgpu_shaders.h
  CMakeLists.txt
src/wasm/viewer_host.cpp                   默认登记 WebGPU
src/wasm/CMakeLists.txt                    --use-port=emdawnwebgpu、ASYNCIFY
```

桌面 `src/app`、设置对话框 **不加** WebGPU 选项。

---

## 4. 明确不做

- 桌面 wgpu-native / Dawn 原生窗口
- 把 bind group / WGSL 抬到 `device.h`
- 用 WebGPU 替换 Vulkan 或现有 OpenGL 4.5
- 几何着色器、clip distance、polygon mode（截面继续 shader `discard`）

---

## 5. 怎么编

需要较新的 [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)（本仓库测过 6.x，已去掉 `-sUSE_WEBGPU`）。浏览器要支持 WebGPU（Chrome / Edge；localhost 或 HTTPS）。

```
cmake --preset wasm
cmake --build --preset wasm-serve
```

产物在 `build/wasm/bin/`。没有 WebGPU 时 `startViewer` 会失败（`RequestAdapter failed`）。
