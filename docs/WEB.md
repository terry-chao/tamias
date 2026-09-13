# 引擎 WASM + Web 查看器

> **状态：阶段 0/1 已落地。** 桌面仍是 Qt 壳；浏览器走另一条产品线：无 Qt 的引擎交叉编译到 WebAssembly，新 Web UI 画在 HTML canvas 上。图形默认 [WebGPU](WGPU.md)（`rhi/webgpu`）。桌面不做 wgpu-native。

实现入口：

- 契约：[`native_window_handle.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/core/native_window_handle.h)、[`document_io.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.h)、[`mesh_io.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/io/mesh_io.h)
- RHI：[`src/engine/render/rhi/webgpu/`](https://github.com/terry-chao/tamias/tree/main/src/engine/render/rhi/webgpu)（默认）；可选 [`src/engine/render/rhi/webgl/`](https://github.com/terry-chao/tamias/tree/main/src/engine/render/rhi/webgl)
- 宿主：[`ViewerHost`](https://github.com/terry-chao/tamias/blob/main/src/wasm/viewer_host.h) + [`web/index.html`](https://github.com/terry-chao/tamias/blob/main/web/index.html)

---

## 0. 为什么不整包搬 Qt

Qt 6 官方支持 WebAssembly，但 Tamias 桌面视口绑的是 HWND / X11、`AA_NativeWindows`、OpenGL 4.5 + SPIR-V、Vulkan Win32/Xlib。这些在浏览器里不存在。

引擎库（document / io / command / entity / modeling）不 include Qt。Web 线复用它们，丢掉 `src/app`。

---

## 1. 阶段划分

| 阶段 | 目标 | 已做 / 未做 |
|---|---|---|
| **0. 契约** | 非 Qt 宿主能喂窗口和字节 | ✅ `NativeWindowHandle.canvas_selector`；`load_document_bytes` / `load_obj_bytes` |
| **1. 查看器** | 浏览器打开 `.tdoc` / `.trscn` / `.obj` 能转 | ✅ WebGPU RHI、同步 `RenderThread::pump`、Vite + React + TS 单页 |
| **2. 轻编辑** | 选中、夹点、撤销 | 🟡 选中（点选 BVH + 高亮）/ 撤销 / 重做 ✅；夹点、框选拖拽 ❌ |
| **3. 建模** | 浏览器里布尔 / 拉伸 | ✅ 走 Truck 内核：柱 / 梁 / 板等命令 + 视口点击落点；圆角倒角 ❌（Truck 没有） |
| **4. BIM** | IFC 浏览 | ❌ 仍走桌面 / 将来服务端 |

阶段 1 **不做**：OCCT 布尔、IFC、多视口、线框 polygon mode。

### 对照桌面端：web 已经有什么

| 能力 | 桌面 | web |
|---|---|---|
| 打开 `.tdoc` / `.trscn` / `.obj` | ✅ | ✅（拖放或选择文件） |
| 转相机 / 框选全部 | ✅ | ✅（中键 / 右键 / F） |
| 线框 / 着色 / 真实模式 | ✅ | ✅（顶栏三个按钮，同一套 `RenderMode`） |
| 点选 + 选中高亮 | ✅ | ✅（左键点击，物体级 BVH；点空白清空） |
| 撤销 / 重做 | ✅ | ✅（`Ctrl+Z` / `Ctrl+Y`） |
| 左下角读数 draw / tri / gpu / tess | ✅ | ✅（视口左下角，字段同名） |
| 建模命令（柱 / 梁 / 板 / 布尔…） | ✅（OCCT / Truck 可选） | ✅（Truck；顶栏放置 + 点击落点） |
| 新建文档 | ✅ | ✅（内置示例场景，几何由内核求值） |
| 删除选中 | ✅ | ✅ |
| 属性面板 / 大纲树 / 测量 / 楼层 / 构件显隐 / 句柄检查 / 渲染场景调试器 | ✅ | ❌ |
| 夹点编辑、框选拖拽、墙工具预览 | ✅ | ❌ |
| 插件（C#） | ✅ | ❌ |

---

## 2. 分层（浏览器）

```
Web UI (web/, Vite + React + TypeScript)
    │  embind：startViewer / loadFile / newDocument / pointer* / renderFrame
    │          / dispatch / undo・redo / selection* / pickEntity / pickWorkPlane
    │          / setRenderMode / stats
    ▼
ViewerHost          相机、文件、提交 FrameSubmission
    ▼
Document / io       与桌面同一套 .tdoc / .trscn / OBJ
    ▼
RenderThread        synchronous=true，主线程 pump()
    ▼
WebGPU RHI          emdawnwebgpu，绑 #viewport
```

描边层是新写的；Document / 命令模型 / `draw_channel` 剧本与桌面相同。

---

## 3. 阶段 0 契约

**窗口。** `NativeWindowHandle` 增加 `canvas_selector`（例如 `"#viewport"`）。Emscripten 下 `valid()` 只看这个字段；Win32 / X11 行为不变。

**内存 IO。**

| API | 用途 |
|---|---|
| `load_document_bytes` | 完整 `.tdoc`（magic + chunk），不设 `Document::path` |
| `load_document` | 读盘后转调上面，并写入 path |
| `deserialize_render_scene` / `document_from_render_scene` | 内存 `.trscn`（烤好的网格 + draw list + 相机），见 [渲染场景快照](RENDER-SCENE.md) |
| `load_obj_bytes` | 内存 OBJ（`v` / `vn` / `f`，无 MTL） |
| `load_mesh_bytes` | 按扩展名分派；阶段 1 只接 `.obj` |

桌面测试：`Io.LoadObjBytesTriangle`、`DocumentIo.LoadDocumentBytesRoundTrip`。

---

## 4. 阶段 1 查看器

**图形。** 默认 `GraphicsBackend::WebGPU`。实现藏在 `rhi/webgpu`，对外仍是 `device.h`。着色器是内嵌 WGSL（`webgpu_shaders.h`），不走 DXC / SPIR-V。Clip 校正与 Vulkan 相同（翻 Y，Z ∈ [0, 1]）。标准 WebGPU 没有 polygon mode，线框模式先走片元 `mode` 分支。可选回退：`TAMIAS_ENABLE_WEBGL_BACKEND` 编 `rhi/webgl`（GLSL ES 3.00）。详见 [浏览器 WebGPU](WGPU.md)。

**线程。** `RenderDeviceConfig.synchronous = true` 时不建 `std::thread`。上传和 `pump()` 都在调用线程跑，避开 SharedArrayBuffer / COOP-COEP。

**OCCT。** WASM 预设关掉 `TAMIAS_ENABLE_OCCT`，没有后端注册进内核注册表：特征树求值会直接报
“kernel not registered”（`geometry_builder` 的过渡 shim 原样把错误传出去）。打开已 tessellate 的
`.tdoc` 只吃 MESH/SCEN 缓存，不重求值。见 [建模内核](MODELING-KERNEL.md)。

**UI。** `web/` 是自建的 Vite + React 18 + TypeScript 工程，依赖全部本地化（不走 esm.sh CDN）。
`src/viewer.ts` 里有 embind 导出的类型声明和加载器（运行时动态加载同目录的 `tamias_viewer.js`）；
`src/App.tsx` 负责顶栏、文件打开、拖放、状态栏、键盘快捷键。构建时把 `dist/` 拷进 wasm 输出目录，
与 `tamias_viewer.js` / `.wasm` 同目录。换 UI 只换 `web/`，不换 `ViewerHost`。

---

## 5. 怎么编、怎么开

需要 [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)。Windows 上 SDK 在 `C:/dev/emsdk`。

**浏览器查看器：按 F5。** Run and Debug 选 **「Tamias: WASM 预览 (F5)」**：按需配置并构建
`tamias_viewer` → 在独立窗口起预览服务 → 打开 http://localhost:3000（并附上 JS 调试器）。
**关掉 `TamiasWasmServe` 窗口就是停止预览**，不需要再切「停止预览」预设。预览服务强制
`Cache-Control: no-store`，不会再出现浏览器缓存住旧 `wasm` 的情况。

再按一次 F5 也安全：已有的预览服务会先被清掉，再重起。

| 想做的事 | 怎么做 |
|---|---|
| 浏览器预览（推荐） | Run and Debug → **Tamias: WASM 预览 (F5)** |
| 停止预览 | 关掉 `TamiasWasmServe` 窗口 |
| 桌面调试 | Run and Debug → **CMake Launch Target**（照旧用 CMake Tools 的 preset） |
| 只构建、不开浏览器 | `powershell -File scripts/wasm-preview.ps1 -NoBrowser` |
| 只构建 wasm | `powershell -File scripts/wasm.ps1` |

命令行（等价于 F5，只是不开浏览器）：

```
powershell -File scripts/wasm-preview.ps1 -NoBrowser
```

CMake Tools 的状态栏里，wasm 只保留一个**构建**预设「构建 WASM 查看器」（Configure
preset 选 `wasm`）——预览统一走 F5，不再有「停止预览」这种预设。想从命令行单独起服务：
`cmake --build build/wasm --target serve_viewer`。产物在 `build/wasm/bin/`。

浏览器里：中键旋转，右键平移，滚轮缩放。顶栏打开 `.tdoc`、`.trscn` 或 `.obj`。没有文件时画演示立方体。`.trscn` 的含义见 [渲染场景快照](RENDER-SCENE.md)。

wasm 构建会自动构建 `web/`（首次若缺少 `node_modules` 会先跑 `npm ci`），
把 Vite 产物 `index.html` + `assets/` 拷到 `build/wasm/bin/`。
只想单独构建 UI：`cd web && npm run build`，然后手动把 `dist/*` 拷到 `build/wasm/bin/`。

不要在 `build/` 里找 `CMakePresets.json`。不要给 wasm 配 vcpkg。

---

## 6. 构建开关

| CMake 选项 | 桌面默认 | WASM |
|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | OFF |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OFF |
| `TAMIAS_ENABLE_WEBGL_BACKEND` | OFF | OFF（可选回退） |
| `TAMIAS_ENABLE_WEBGPU_BACKEND` | OFF | ON |
| `TAMIAS_ENABLE_OCCT` | ON | OFF |
| `TAMIAS_BUILD_TESTS` | ON | OFF |

`src/app`、`src/command`、IfcOpenShell 在 EMSCRIPTEN 下不进构建。

---

## 7. 明确不做（阶段 1）

- 不把现有 Qt Ribbon / 属性面板编进 WASM
- 不在客户端链 OCCT / IfcOpenShell / Boost
- 不上 Vulkan / 桌面 OpenGL 4.5
- 不要求 SharedArrayBuffer
- 不在这一阶段做服务端网格下发（那是另一条可选路径）

---

## 8. 下一步

1. 阶段 2：embind `CommandSystem` + CPU 拾取（现有 BVH）。
2. 阶段 3：裁剪 OCCT 交叉编译，求值放 Worker。
3. 阶段 4：大 IFC 走服务端，或按需加载独立 wasm 模块。
