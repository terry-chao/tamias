# 引擎 WASM + Web 查看器

> **状态：阶段 0/1 已落地。** 桌面仍是 Qt 壳；浏览器走另一条产品线：无 Qt 的引擎交叉编译到 WebAssembly，新 Web UI 画在 HTML canvas 上。第三桌面后端 [wgpu](WGPU.md) 与这条线独立。

实现入口：

- 契约：[`native_window_handle.h`](../src/engine/core/native_window_handle.h)、[`document_io.h`](../src/engine/document/document_io.h)、[`mesh_io.h`](../src/engine/io/mesh_io.h)
- RHI：[`src/engine/render/rhi/webgl/`](../src/engine/render/rhi/webgl/)
- 宿主：[`ViewerHost`](../src/web/viewer_host.h) + [`web/index.html`](../web/index.html)

---

## 0. 为什么不整包搬 Qt

Qt 6 官方支持 WebAssembly，但 Tamias 桌面视口绑的是 HWND / X11、`AA_NativeWindows`、OpenGL 4.5 + SPIR-V、Vulkan Win32/Xlib。这些在浏览器里不存在。

引擎库（document / io / command / entity / modeling）不 include Qt。Web 线复用它们，丢掉 `src/app`。

---

## 1. 阶段划分

| 阶段 | 目标 | 已做 / 未做 |
|---|---|---|
| **0. 契约** | 非 Qt 宿主能喂窗口和字节 | ✅ `NativeWindowHandle.canvas_selector`；`load_document_bytes` / `load_obj_bytes` |
| **1. 查看器** | 浏览器打开 `.tdoc` / `.obj` 能转 | ✅ WebGL2 RHI、同步 `RenderThread::pump`、React 单页 |
| **2. 轻编辑** | 选中、夹点、撤销 | ❌ `CommandSystem` 尚未 embind |
| **3. 建模** | 浏览器里布尔 / 拉伸 | ❌ OCCT 未进 WASM |
| **4. BIM** | IFC 浏览 | ❌ 仍走桌面 / 将来服务端 |

阶段 1 **不做**：OCCT 布尔、IFC、多视口、线框 polygon mode。

---

## 2. 分层（浏览器）

```
Web UI (web/index.html, React)
    │  embind：startViewer / loadFile / pointer* / renderFrame
    ▼
ViewerHost          相机、文件、提交 FrameSubmission
    ▼
Document / io       与桌面同一套 .tdoc / OBJ
    ▼
RenderThread        synchronous=true，主线程 pump()
    ▼
WebGL2 RHI          GLES 3，绑 #viewport
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
| `load_obj_bytes` | 内存 OBJ（`v` / `vn` / `f`，无 MTL） |
| `load_mesh_bytes` | 按扩展名分派；阶段 1 只接 `.obj` |

桌面测试：`Io.LoadObjBytesTriangle`、`DocumentIo.LoadDocumentBytesRoundTrip`。

---

## 4. 阶段 1 查看器

**图形。** `GraphicsBackend::WebGL`。实现藏在 `rhi/webgl`，对外仍是 `device.h`。着色器是内嵌 GLSL ES 3.00（`webgl_shaders.h`），不走 DXC / SPIR-V。Clip 校正与桌面 OpenGL 相同（Z 从 `[0,1]` 映到 `[-1,1]`）。WebGL 没有 `glPolygonMode`，线框模式先画成实体着色。

**线程。** `RenderDeviceConfig.synchronous = true` 时不建 `std::thread`。上传和 `pump()` 都在调用线程跑，避开 SharedArrayBuffer / COOP-COEP。

**OCCT。** WASM 预设关掉 `TAMIAS_ENABLE_OCCT`，`modeling` 链 `stub_geom_builder.cpp`。打开已 tessellate 的 `.tdoc` 只吃 MESH/SCEN 缓存，不重求值。

**UI。** `web/index.html` 用 React 18（esm.sh）做顶栏 + canvas。C++ 通过 embind 暴露函数，不依赖 Qt。以后换成自建 React/Vite 工程时，只换壳，不换 `ViewerHost`。

---

## 5. 怎么编、怎么开

需要 [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)。Windows 上 SDK 在 `C:/dev/emsdk`。

**CMake Tools（推荐）：** 状态栏按套切换，不要用 Run and Debug 里的独立 task。

| 项 | 桌面 | 浏览器查看器 |
|---|---|---|
| Configure Preset | `msvc` | `wasm`（Emscripten WebGL viewer） |
| Build Preset | `debug` | `wasm-serve` 编译并开 http://localhost:3000；`wasm-stop` 停预览 |
| Launch Target | `tamias` | `tamias_viewer` |

切到 `wasm` 后跑一次 **CMake: Configure**，再 **CMake: Build**。预览选 build preset **WASM 预览 (localhost:3000)** 然后 Build；停预览选 **WASM 停止预览** 再 Build，或关掉弹出的 `TamiasWasmServe` 窗口。不要按 Debug（`tamias_viewer` 不是 exe，cppvsdbg 跟不了）。看完桌面再切回 `msvc` / `debug` / `tamias`。

命令行（可选，等价于上面的 preset）：

```
cmake --preset wasm
cmake --build --preset wasm-serve
cmake --build --preset wasm-stop
```

或 `powershell -File scripts/wasm.ps1`。产物在 `build/wasm/bin/`。

浏览器里：左键旋转，右键平移，滚轮缩放。顶栏打开 `.tdoc` 或 `.obj`。没有文件时画演示立方体。

不要在 `build/` 里找 `CMakePresets.json`。不要给 wasm 配 vcpkg。

---

## 6. 构建开关

| CMake 选项 | 桌面默认 | WASM |
|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | OFF |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OFF |
| `TAMIAS_ENABLE_WEBGL_BACKEND` | OFF | ON |
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
