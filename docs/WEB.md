# 引擎 WASM + Web 查看器

> **状态：⚠️ 施工中（WIP）。** 查看器、轻编辑、建模已经能跑，但**和桌面端远未对齐**：
> 功能在陆续补，embind 接口与交互细节随时可能变，别当成品对外承诺。流程是
> **登录 → 文件管理 → 双击一份文档 → 查看器**；顶栏照桌面端做的（一行菜单 + 两段分区
> 工具带 + 底部状态栏），43 个工具，但菜单里只放真的能用的命令。
> **造型内核默认 OCCT**（7.9.3 交叉编译进 wasm），Truck 是可选回退（预设 `wasm-truck`）。
> 桌面仍是 Qt 壳；浏览器走另一条产品线：无 Qt 的引擎交叉编译到 WebAssembly，新 Web UI 画在 HTML canvas 上。图形**默认 WebGL2**（`rhi/webgl`）；[WebGPU](WGPU.md)（`rhi/webgpu`）是**实验**预设 `wasm-webgpu`，功能与稳定性尚未与 WebGL 对齐。桌面不做 wgpu-native。

实现入口：

- 契约：[`native_window_handle.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/base/native_window_handle.h)、[`document_io.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.h)、[`mesh_io.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/io/mesh_io.h)
- RHI：[`src/engine/render/rhi/webgl/`](https://github.com/terry-chao/tamias/tree/main/src/engine/render/rhi/webgl)（**默认**，GLSL ES 3.00）；实验可选 [`src/engine/render/rhi/webgpu/`](https://github.com/terry-chao/tamias/tree/main/src/engine/render/rhi/webgpu)（WGSL，预设 `wasm-webgpu`）
- 宿主：[`ViewerHost`](https://github.com/terry-chao/tamias/blob/main/src/wasm/viewer_host.h) + [`web/index.html`](https://github.com/terry-chao/tamias/blob/main/web/index.html)
- 前端：[`web/src/`](https://github.com/terry-chao/tamias/tree/main/web/src)——`App.tsx`（登录门禁 + 引擎预加载）、`ViewerApp.tsx`（查看器本体）、`files/`（文件管理页）、`ribbon/`（顶栏与工具带）、`engine/EngineLoading.tsx`（加载画面）、`modeling/catalog.ts`（工具目录）

---

## 0. 为什么不整包搬 Qt

Qt 6 官方支持 WebAssembly，但 Tamias 桌面视口绑的是 HWND / X11、`AA_NativeWindows`、OpenGL 4.5 + SPIR-V、Vulkan Win32/Xlib。这些在浏览器里不存在。

**而且「用 Qt 就不用管图形 API」也不成立：Qt 在浏览器里只能跑 WebGL。** Qt for WebAssembly 的硬件加速就是 WebGL（GLES 2.0 → WebGL 1、GLES 3.0 → WebGL 2），Qt 自己的 QRhi 后端只有 `Null / Vulkan / OpenGLES2 / D3D11 / D3D12 / Metal`，**没有 WebGPU**。搬 Qt 进浏览器等于把整个产品锁在 WebGL2 上，wasm 侧的 WebGPU 路线一起没了；桌面那套 Vulkan / GL 4.5 假设也一条都不成立。

Qt 官方文档同时写明这些限制（[Qt for WebAssembly](https://doc.qt.io/qt-6/wasm.html)、[QRhi](https://doc.qt.io/qt-6/qrhi.html)）：

| 限制 | 影响 |
|---|---|
| 动态库 / 动态链接是 Technology Preview，原话 “not suitable for production use”；该模式下多线程与 Asyncify 不支持 | 打包与分发策略要按单体静态链接设计 |
| 默认不支持嵌套事件循环：`QDialog::exec()`、带返回值的 `QMessageBox`、向外拖拽都要 Asyncify 或 JSPI；`QApplication::exec()` 永不返回、栈被泄漏 | 模态对话框这类 CAD 高频交互要重写；Asyncify 的代价见 [Emscripten 文档](https://emscripten.org/docs/porting/asyncify.html)：体积与速度「something like 50% or so」 |
| `QOpenGLWidget` 不支持；OpenGL 上下文共享不支持（`shareContext()` 恒为 `nullptr`）；**一个 surface 只能一个 WebGL context** | 自研视口接进去只剩「Qt 建 context、RHI attach 上去」一条路，得把 `WebGLDevice` 的 initialize 拆成 attach 模式 |
| 渲染到单个 `<canvas>`，没有 DOM | 无障碍靠 shadow DOM 补且默认关闭；系统字体拿不到，字体要随包发 |
| 多线程要 SharedArrayBuffer + 跨源隔离（COOP `same-origin` / COEP `require-corp`） | 部署要改响应头；Qt 计划长期保留无线程模式（[开发邮件列表，2024-06](https://lists.qt-project.org/pipermail/development/2024-June/045385.html)） |

**体积。** Qt 官方给的典型下载量：Core+Gui 2.1 MB brotli、加 Widgets 3.2 MB、再加 Quick+Charts 6.3 MB。真实 CAD 量级看唯一能找到的整包 Qt 编译案例 LibreCAD-Web：约 **18 MB brotli**（39 MB 原始 wasm + 30 MB 数据包；爱好项目、作者自述非厂商验证，[magik.net/librecad](https://magik.net/librecad/)）。对标 Tamias 现在的产物：`tamias_viewer.wasm` **26.2 MB**（25.0 MiB，含交叉编译进来的 OCCT）、前端脚本 + 样式约 290 KB、示例文档约 7 MB。**把 OCCT 关掉就回到 2.8 MB 量级**（预设 `wasm-truck`；OCCT 切换前实测 2.79 MB）——这份体积账是"要不要在浏览器里自己造型"的直接代价，见 §1 与 §6。

**产业界怎么做的**（每条都能追到一手出处）：

| 产品 / 项目 | 形态 | 出处 |
|---|---|---|
| rhino3dm.js | wasm 内核（OpenNURBS）+ JS API | [developer.rhino3d.com](https://developer.rhino3d.com/guides/opennurbs/what-is-rhino3dmio/) |
| web-ifc（IFC.js） | C++ 内核 → wasm（Emscripten）+ 薄 JS 层 | [github.com/ifcjs/web-ifc](https://github.com/ifcjs/web-ifc) |
| Speckle viewer | 纯 JS/TS，建在 three.js 上 | [docs.speckle.systems](https://docs.speckle.systems/developers/viewer/overview) |
| ODA inWEB | JS 查看器；wasm 只用在约束求解器 / BREP 建模器的原型 | [opendesign.com/products/inweb](https://www.opendesign.com/products/inweb) |
| Autodesk APS | wasm 是**组件**不是几何内核（Rust→wasm 在 Service Worker 里解密模型流） | [aps.autodesk.com](https://aps.autodesk.com/blog/load-encrypted-model-data-webassembly-and-rust) |
| Onshape | 几何在服务端，客户端只负责渲染与交互（fillet 只回传变化的三角面） | [cad.onshape.com/help](https://cad.onshape.com/help/Content/Home/hardware_and_graphics_performance_recommendations.htm) |
| Rhino Compute / ShapeDiver | **不是**浏览器内 wasm：headless Rhino.Inside 服务 / AWS 上的 SaaS，客户端只是 JS + glTF 查看器 | [help.shapediver.com](https://help.shapediver.com/doc/shapediver-rhino-compute) |

**没有找到任何一个已出货的商业 CAD / BIM 产品跑整包 Qt-for-wasm。** 注意这是「未证实的缺席」，不是「证明不存在」——但结论方向很清楚：出货产品要么把 C++ 内核放服务端，要么做成聚焦的 wasm 库 + web 原生 UI，「整包原生 UI 工具链进浏览器」在 CAD 领域只出现在爱好项目里。

**一处边界**：上面是**现象**的证据；「他们是因为 Qt 这些限制才这么选」是**推断**，没有厂商公开这么说过。AutoCAD Web / SketchUp for Web 也没有找到 wasm vs JS 的一手说明，别当事实引用。

引擎库（document / io / command / entity / modeling）不 include Qt。Web 线复用它们，丢掉 `src/app`。真要做到「一份 UI 两端跑」，走 [tac](TAC.md) 契约（`src/ui/tac` 无 Qt，wasm 分支已经在构建它），不要把 Qt 拖进浏览器。

---

## 1. 阶段划分

| 阶段 | 目标 | 已做 / 未做 |
|---|---|---|
| **0. 契约** | 非 Qt 宿主能喂窗口和字节 | ✅ `NativeWindowHandle.canvas_selector`；`load_document_bytes` / `load_obj_bytes` |
| **1. 查看器** | 浏览器打开 `.tdoc` / `.trscn` / `.obj` 能转 | ✅ WebGL2 RHI（默认）、同步 `RenderThread::pump`、Vite + React + TS 单页、登录 → 文件管理 → 文档 |
| **2. 轻编辑** | 选中、夹点、撤销 | 🟡 选中（点选 BVH + 高亮）/ 撤销 / 重做 / 删除选中 ✅；夹点、框选拖拽 ❌ |
| **3. 建模** | 浏览器里拉伸 / 布尔 / 圆角 | ✅ **OCCT 交叉编译进 wasm**：43 个工具，按桌面端的分组排（文件 / 编辑 / 绘制 / 建筑 / 结构 / 修改 / 注释 / 导航 / 轴网 / 楼层 / 帮助，视图段 显示 / 面板）。点采集与 args 拼接在 web 侧完成（命令都支持"直给点"，所以引擎不需要 `feed_point`）；❌ 没有 `DragContext` 那种实时实体预览，只有屏幕空间橡皮筋 |
| **4. BIM** | IFC 浏览 | ❌ 仍走桌面 / 将来服务端（IfcOpenShell 已从 OCCT 开关上解耦，见 §6） |

阶段 1 **不做**：IFC、多视口、线框 polygon mode。

### 对照桌面端：web 已经有什么

| 能力 | 桌面 | web |
|---|---|---|
| 打开 `.tdoc` / `.trscn` / `.obj` | ✅ | ✅（文件管理页双击 / 拖放 / 顶栏「打开」） |
| 转相机 / 框选全部 | ✅ | ✅（中键 / 右键 / `F`；右上角朝向立方体可点面、**也可点 8 个角**，与桌面一致） |
| 线框 / 着色 / 真实模式 | ✅ | ✅（工具带「显示」组，同一套 `RenderMode`） |
| 点选 + 选中高亮 | ✅ | ✅（物体级 BVH；点空白清空） |
| 撤销 / 重做 / 删除选中 | ✅ | ✅（`Ctrl+Z` / `Ctrl+Y`；删除支持多选） |
| 左下角读数 draw / tri / gpu / tess | ✅ | ✅（字段同名，另加 backend / kernel 两个读数） |
| **造型内核** | ✅ OCCT（Truck 可选） | ✅ **OCCT（默认）**，Truck 是可选回退 |
| 绘制 / 建筑 / 结构构件 | ✅ | ✅ 墙 / 门 / 窗 / 幕墙 / 梁 / 柱 / 板 / 结构墙 / 基础 + 直线 / 折线 / 矩形 / 圆 / 圆弧 / 贝塞尔 / B 样条 / 文字 |
| 修改（移动 / 复制 / 旋转 / 镜像 / 阵列） | ✅ | ✅（对当前选择集逐条发命令，逐条可撤销） |
| 圆角 / 倒角 / 布尔 | ✅ | ✅（由 OCCT 提供） |
| 轴网 / 楼层 | ✅ | 🟡 自动轴网 / 加轴线 / 柱网 / 新建楼层能建；**轴线 id 拿不到**，所以柱网只能按整张轴网布；没有楼层管理面板 |
| 文档模型 | ✅ 多标签页 | 🟡 **一个网页标签页 = 一份文档**（有意不做页内多标签）。有新建 / 打开 / 关闭（`Ctrl+W`）/ 最近打开 / 文件管理页 |
| 保存 / 另存为 / 导出快照 | ✅ | ❌ 浏览器不允许网页往磁盘写文件（要真做得上 `showSaveFilePicker`） |
| X 光 / 正交 / 轴网显示 / 标注显示开关 | ✅ | ❌ **引擎侧已有实现，缺 embind 导出 + 按钮**：`Camera::set_orthographic()` 与 `ViewerHost::xray_` 都在，只是 JS 拿不到（旧文档写成「引擎侧还没有」是错的） |
| 属性 / 材质 / 纹理 / 手柄 / 楼层 / 构件显隐面板 | ✅ | ❌ 桌面宿主专属 |
| 打开图纸（DWF / DXF / PDF）、插件（C#）、命令控制台、AI 面板、图形诊断 | ✅ | ❌ 桌面宿主专属，或 web 侧没有对应入口 |
| 夹点编辑、框选拖拽、实时实体预览 | ✅ | ❌（放置流程只有屏幕空间橡皮筋，见 §4） |
| 文字注记 / 轴号 / 标高 | ✅ | ❌ **能建、能存、能撤销，但浏览器里不画**：WebGL 的 text / overlay / line 三条 GLSL 管线已补（2026-10-22），但 `ViewerHost::render()` 还不提交 `text_quads` / `drawing_overlays`，而且 wasm 没有字体/图集来源。见 [WebGL 链路审计](WEBGL-PIPELINE-AUDIT.md) §2① |
| 跨刷新的「最近打开」 | ✅ | ❌ 存的是 `File` 句柄，不可序列化，只在本次会话内有效 |
| 按路径导入网格（`import_mesh`） | ✅ | ❌ 沙箱里没有路径，只能靠选择文件或拖放 |
| `create_beam` 的 T / I 断面 | ✅ | ❌ 那两个几何参数是交互态下算出来的，命令层没有对应入参 |

---

## 2. 分层（浏览器）

```
Web UI (web/, Vite + React + TypeScript)
    │  embind（34 个导出，按用途分组）：
    │    生命周期  startViewer / newDocument / loadFile / closeDocument / hasDocument / documentName
    │    读数      status / stats / renderMode / kernelName / backendName / backendExperimental / logText
    │    交互      pointerDown / pointerMove / pointerUp / wheel / pickEntity / pickWorkPlane / resizeViewer
    │    相机      frameAll / viewYaw / viewPitch / setViewAngles
    │    命令      dispatch / undo / redo / canUndo / canRedo
    │    选择      clearSelection / selectionCount / selectionIdAt
    │    渲染      renderFrame / setRenderMode
    ▼
ViewerHost          相机、文档生命周期、提交 FrameSubmission
    ▼
Document / io       与桌面同一套 .tdoc / .trscn / OBJ
    ▼
tamias::host        与桌面同一个宿主层（所以 src/command 也在 wasm 里）
    ▼
RenderThread        synchronous=true，主线程 pump()
    ▼
WebGL2 RHI           rhi/webgl，绑 #viewport
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

**图形。** 默认 `GraphicsBackend::WebGL2`。实现藏在 `rhi/webgl`，对外仍是 `device.h`。着色器是内嵌 GLSL ES 3.00（`webgl_shaders.h`），不走 DXC / SPIR-V。**链接必须显式抬到 WebGL 2**：Emscripten 的 `MIN/MAX_WEBGL_VERSION` 缺省都是 1，而 rhi/webgl 用的 UBO 那一族（`glBindBufferBase` / `glGetUniformBlockIndex` / `glUniformBlockBinding`）是 WebGL2 才有的——不抬的话编译全过、**最后一步链接**才报 undefined symbol。实验可选：`wasm-webgpu` 预设编 `rhi/webgpu`（内嵌 WGSL `webgpu_shaders.h`，emdawnwebgpu）。详见 [浏览器 WebGPU](WGPU.md)。

**线程。** `RenderDeviceConfig.synchronous = true` 时不建 `std::thread`。上传和 `pump()` 都在调用线程跑，避开 SharedArrayBuffer / COOP-COEP。

**造型内核。** 默认 **OCCT**，Truck 是可选回退——和桌面 / 服务端同一个关系
（`linked_kernels.cpp` 的默认后端就是 `Occt`，没有就回退到第一个注册进来的）。

浏览器里要有 OCCT，前提是一份**交叉编译到 wasm32-emscripten** 的 OCCT，
只含 `FoundationClasses + ModelingData + ModelingAlgorithms`
（没有 Visualization / DataExchange）。编法：

```powershell
powershell -File scripts/occt-wasm.ps1     # 产物默认在 C:\dev\occt-wasm\prefix
```

再由 `TAMIAS_OCCT_PREFIX` 指给构建（`wasm` 预设里已经指向那个默认位置）。两条容易踩的：

- **`TAMIAS_OCCT_DATA_EXCHANGE=OFF`**（wasm 预设就是这么设的）：STEP / IGES / BREP 导入
  （`occt_shape_ops.cpp`）整份不编。它要 XCAF，而 XCAF 在 OCCT 7.9 上会把 Visualization
  一起拖进来；浏览器本来也没有这条入口（加载只认 `.tdoc` / `.trscn` / `.obj`）。
- **`TAMIAS_ENABLE_OCCT` 不能在 `EMSCRIPTEN` 分支里 FORCE 关。** 以前就是那么写的，
  结果无论预设怎么设，wasm 里永远只有 Truck。现在它和 RHI 后端一样是个普通开关。
  另外 Emscripten 的工具链把 `CMAKE_FIND_ROOT_PATH_MODE_PACKAGE` 设成 `ONLY`，
  `CMAKE_PREFIX_PATH` 会被整个无视——`TAMIAS_OCCT_PREFIX` 因此也要进
  `CMAKE_FIND_ROOT_PATH`（`TamiasDependencies.cmake` 里做了），否则报的是
  "Could NOT find OpenCASCADE"，连候选文件都不列。

Truck 仍是**可选**那一个：`wasm-truck` 预设（`TAMIAS_ENABLE_OCCT=OFF`）出 2.8 MB 的轻量产物，
默认的 `wasm` 带 OCCT、26 MB 量级。两个都编进来时 OCCT 胜出；若设了
`set_default_kernel_backend(occt)` 而这次构建没编进 OCCT，引擎日志里会有一条
`falling back to 'truck'` 的 warn，`kernelName()` 也如实报 `truck`。

打开已 tessellate 的 `.tdoc` 只吃 MESH/SCEN 缓存，不重求值。见 [建模内核](MODELING-KERNEL.md)。

**UI。** `web/` 是自建的 Vite + React 18 + TypeScript 工程，依赖全部本地化（不走 esm.sh CDN）。
`src/viewer.ts` 里有 embind 导出的类型声明和加载器（运行时动态加载同目录的 `tamias_viewer.js`）；
`src/App.tsx` 是登录门禁（**纯前端 UI 门禁，不是安全边界**，理由见 `src/auth.ts` 顶部），
`src/ViewerApp.tsx` 是查看器本体（视口、拖放、键盘快捷键、底部状态栏、放置流程），
`src/files/` 是**文件管理页**（登录后的落地页：示例 / 最近打开 / 本地文件夹，双击打开），
`src/modeling/catalog.ts` 是工具清单，
`src/ribbon/` 是**照桌面端做的顶栏**：一行菜单 + 两段分区工具带 + 命令搜索，
配色与尺寸对着 `src/app/shell/ribbon/ribbon_bar.cpp` 与 `src/app/base/theme.cpp` 抄，
排法见 [APP.md](APP.md) 的「顶栏」一节。构建时把 `dist/` 拷进 wasm 输出目录，
与 `tamias_viewer.js` / `.wasm` 同目录。换 UI 只换 `web/`，不换 `ViewerHost`。

**登录与登录态。** `src/auth.ts` 是登录态的唯一来源，`src/App.tsx` 只做门禁。
会话存在 **`localStorage`**（同源所有标签页共享），所以**登录之后新开标签页不用再登一次**。
踩过的坑：原来用 `sessionStorage`，而它是**按标签页隔离**的——每开一个标签页必然要求重新
登录。配套三件事：

1. **一周有效期**（`SESSION_TTL_MS`），每次打开页面顺延一次（滑动过期），常用的人不会掉线。
   取一周是因为它是本地工具而非对外服务；代价是**共用电脑上"退出登录"必须真的点**；
2. **跨标签页同步**：在别的标签页登出，这一页也跟着登出。用 `storage` 事件——注意它
   **只在"别的"标签页里触发**，发起变更的那一页收不到，得自己 `setState`；
3. **`storageAvailable()` 探测存储可用性**：隐私模式 / 站点数据被禁时登录态根本留不住，
   登录页会**明说**这一点，否则看起来像登录逻辑写错了。

**但它仍然不是安全边界**：凭据、判定、存储全在浏览器里，DevTools 里写一句
`localStorage.setItem("tamias.session", ...)` 就能绕过。真鉴权必须在服务端
（见 [服务端语言决策](DECISION-SERVER-LANGUAGE.md) §8.2：服务端目前无认证）。

**图标只有一份来源。** 工具带图标直接用桌面端那套 `assets/icons/*.svg`
（资源清单是 `src/app/resources.qrc`），经 `import.meta.glob(..., { query: "?url" })`
在构建期内联成 data URL。它们是**全彩插画**（渐变 + 硬编码颜色），不是单色字形，
所以用 `<img>` 而不是内联 SVG——顺便避开"多个 SVG 都有 `id="top"` 内联后串色"的坑。
Vite 的 `server.fs.allow` 要放开 `web/` 之外，因为图标在仓库根目录。

**工具目录。** `src/modeling/catalog.ts` 是工具的唯一来源：每个工具有 命令名 / 中文标签 /
图标 basename / 分组 / 要几个点 / 需要哪些参数 / 是否要选择集。`ViewerApp` 把它映射成
桌面端同款的两段分区（绘制 7 / 建筑 4 / 结构 6 / 修改 11 / 轴网 3 / 注释 1 / 楼层 1 +
文件 / 编辑 / 导航 / 帮助，视图段 显示 / 面板），共 43 个工具。

**放置流程（web 自己实现）。** 这些命令**全都支持"直给点"**（`p:points=` 或
`v:origin` / `v:position`），所以 web 端按 `pickWorkPlane` 采点、把 args 拼全再
`dispatch` 就够了——**引擎侧不需要 `feed_point` / `DragContext`**。代价是没有桌面端那种
"跟着鼠标长的实体预览"，只有屏幕空间的橡皮筋（`src/ribbon/RubberBand.tsx`）：
用户点过的点当初就是从那个屏幕位置反投影到工作平面的，所以连起来就是正确的投影。
Enter / 双击结束开放式工具（折线 / 墙 / 板），Esc 取消。

**文档模型：一个网页标签页 = 一份文档，而且进来先落在「文件管理」。**
完整流程是**登录 → 文件管理 → 双击一份文档 → 查看器**。所以引擎启动时**不建默认文档**
（`ViewerHost::start()` 只起渲染通道，`has_document()` 为 false，`render()` 提交空帧），
由用户在文件管理页决定开哪一份。文件菜单里有「新建 / 打开 / 关闭文档（`Ctrl+W`）/
最近打开」，顶栏右端是**当前文档 chip**（名字 + 关闭叉），底部状态栏只留消息。

**所以"怎么同时开第二份文档"= 开第二个浏览器标签页**（这是这个模型的直接推论，不是缺口）：

- 登录态在 `localStorage` 里，**新标签页不用重登**；
- 示例文档有 URL 深链 **`?doc=sample:<key>`**（`src/viewer/deepLink.ts`）：打开示例时地址栏
  会同步成它（`history.replaceState`，不堆历史），所以卡片是**真 `<a href>`**——
  Ctrl/⌘/Shift+点击、中键、浏览器右键「在新标签页打开」全都天然可用，另有同名的右键菜单项。
  关闭文档 / 打开本地文件 / 新建时会清掉参数（地址栏不许撒谎）；
- **本地文件没有深链**：浏览器不给本地路径，写不进 URL。要在新标签页里继续用同一份本地
  文件，得把 `FileSystemFileHandle` 存进 IndexedDB（句柄是可结构化克隆的）——那是另一条
  路，顺带能解决"最近打开的跨刷新"，见下面「最近打开」那条；
- 「最近打开」是**每个标签页各自一份内存**（存 `File` 句柄，不可序列化），所以新标签页的
  列表是空的，得重新找文件。

**登录页**默认把演示凭据填好（`auth.ts` 导出 `DEMO_USER` / `DEMO_PASSWORD`，登录页只管
默认值，不另抄一份），进来直接回车即可。排版是常规双栏：左侧大面积品牌展示（logo +
定位句取 README 第一句 + 三条能力点），右侧账号密码；背景的品牌光晕与**等轴测网格是纯
CSS 画的**（`repeating-linear-gradient` 叠三组线 + `mask` 淡出），不引额外素材。
≤900px 堆成一列。它仍然只是**纯前端 UI 门禁**，不是安全边界。

**引擎预加载提前到登录页。** `tamias_viewer.wasm` 26 MB，别等用户点完登录才开始下：
`App` 一挂载就调 `preloadViewer()`，`ViewerApp` 之后调 `loadViewer()` 复用同一个 promise。
进度是自己 `fetch` 读 `response.body` 报的（拿不到 `Content-Length` 就退化成不确定进度），
拿到字节后通过 **`wasmBinary` 交给 MODULARIZE 工厂**——胶水里的 `getWasmBinary` 见到它就
跳过自己的 fetch，所以不会白下第二遍（预览服务强制 `no-store`，这一点很关键）。
引擎没起来时视口位置显示 `src/engine/EngineLoading.tsx`（转圈 + 进度 + 失败可重试），
不再是一块空白。

**「下完了」由阶段决定，不由字节数推。** 预加载有 `download → instantiate → ready` 三个阶段
（`EnginePhase`），`ready` 由**那个 promise 的兑现**置位。踩过的坑：原来把"下完了"判定成
`total > 0 && loaded >= total`，而预览服务当时流式发 `.wasm`、**不发 `Content-Length`**
（`total` 恒为 0），条件永远不成立——引擎早就可用了，登录页还一直挂着"正在预热引擎…"。
另外两条同理：字节下完 ≠ 引擎可用（26 MB 还要实例化几秒），而重复调用会命中缓存的 promise
（`onProgress` 一次都不回调）。预览服务现在会显式发 `Content-Length`（文件已经整个读进内存，
长度是已知的），所以本地能看到真实百分比——但就绪判定**仍然不依赖它**。

**文件管理页**（`src/files/`）在"没有打开的文档"时盖住视口，排版**照桌面端欢迎页**
（`src/app/shell/home_page.cpp`）：不是表格，是**缩略图卡片墙** + 分区标题行（左粗体标签、
右灰提示）。配色与几何逐条抄自 `home_page_stylesheet(dark)`——页面底 `#1e1f22`、卡片
`#2b2d30` / 边 `#3c3f41` / 2px 圆角、悬浮与选中边 `#3d8bd4`；缩略图按 200×112 比例，
web 没有真缩略图就用 `make_thumb_icon()` 的同款兜底（`#2d2f32` 底 + 居中 48×48 的 logo，
同一份 `assets/branding/logo.png`）。**这一页没有工具栏**——桌面端的欢迎页也没有，
新建 / 打开在顶栏「文件」组与文件菜单里。三个来源各成一节，空的来源不画：

| 来源 | 从哪来 | 说明 |
|---|---|---|
| 最近打开 | 本次会话打开过的（存 `File` 句柄） | 右键可「从最近打开移除」；上限 8，与桌面端 `kMaxRecentFiles` 一致 |
| 本地文件夹 | `showDirectoryPicker()`（Chromium 系） | 列出目录里的 `.tdoc / .trscn / .obj`。**没有这个 API 就整个不出现**（菜单项也不画） |
| 示例 | `assets/samples/**` 的 `.tdoc` / `.trscn` / `.obj`，经 Vite glob 拿 URL | **双击时才 `fetch`**，不然一进页面就把那 3 个 2 MB 的 `.trscn` 全拉下来；`render/**/debug/` 下的测试夹具排除在外 |

交互照桌面端：**单击选中、双击打开**，选中后按 Enter 也行。时间写法也是桌面端
`format_opened_time()` 的同款：`今天, HH:mm` / `昨天, HH:mm` / 短日期。仓库里**没有
`.tdoc` 示例**，所以卡片里的 `.tdoc` 只会来自"打开文件 / 打开文件夹"。注意示例文档会作为
独立资源打进 `dist/assets/`（约 7 MB），随 wasm 一起发布。

- **关闭文档**：丢掉几何与 GPU 资产，回到文件管理页。关之前会确认；如果这份文档有未撤销
  的改动，确认框会**明说改动会丢**——因为 web 端还不能保存（见下表）。
  `ViewerHost::close_document()` 会把当前 Document 换成一份空的，所以 `render()` 提交的是
  空场景，不需要"没有文档"的分支。
- **换文档必须清掉"文档派生的渲染状态"**。`RenderThread::release_all_assets()` 一起清两样，
  两样都是做这个功能时发现并修掉的既有 bug：
  1. **网格 / 纹理映射**：语义 asset id 是每份 `Document` 各自从 1 编号的
     （`Document::next_mesh_id_`），而映射按 asset id 缓存、**命中即早退**
     （`upload_mesh_on_thread`）——不清就是"先开 A 再开 B，B 显示 A 的几何"。
  2. **各通道留存的场景图**：场景图只在 `frame.scene_generation != channel.scene_generation`
     时才重建（`draw_channel` 里那段），而代次是**每份 Scene 各自计数**的，换文档后
     重新计数。撞上相同值那条判断就不成立，**旧几何继续留在屏幕上**（"文档关掉了但
     东西还在"）。清掉 `scene_root` 后下一次提交走整树重建分支；注意 `scene_nodes`
     里存的是树内节点裸指针，必须和 `scene_root` 一起清。
- **最近打开**存的是 **`File` 句柄本身**（只是指向磁盘上那个文件，不把内容读进内存），
  点它时再 `arrayBuffer()` 读一次，所以这条是真能用的。代价：只在本次会话内有效。
- 顶栏文档名与状态栏的分工照桌面端：**名字在标签上**，状态栏只放消息。

**web 端做不到的（有意不放死按钮）。** 逐条清单与原因**并进了 §1 的「对照桌面端」表**——
想补某一项之前先看那张表：它们不是"还没排上"，而是这条路今天不通（缺 embind 导出 /
沙箱里没有文件系统 / 桌面宿主专属）。三条最容易误判的单独拎出来：

- **保存 / 另存为**：wasm 侧**根本没有保存导出**（`viewer_main.cpp` 只有加载），不是 UI 没画。
  要真做得上 File System Access API 的 `showSaveFilePicker`。
- **跨刷新的「最近打开」**：存的是 `File` 句柄本身（不可序列化），要跨刷新得换 File System
  Access 的句柄 + IndexedDB 存句柄，且只在 Chromium 系可用。现在只保证本次会话内有效。
- **轴网轴线 id**：没有对应导出，所以「柱网」只能按整张轴网布，不能只挑几根轴线。

---

## 5. 怎么编、怎么开

需要 [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)。Windows 上 SDK 在 `C:/dev/emsdk`。

**浏览器查看器：按 F5。** Run and Debug 选 **「WebGL + .NET Server」**：按需配置并构建
`tamias_viewer` → 起 .NET 几何服务（`127.0.0.1:5199`）→ 在独立窗口起预览服务 →
打开 http://localhost:3000（并附上 JS 调试器）。
**关掉 `TamiasWasmServe` 窗口就是停止预览**，不需要再切「停止预览」预设。预览服务强制
`Cache-Control: no-store`，不会再出现浏览器缓存住旧 `wasm` 的情况。

再按一次 F5 也安全：已有的预览服务会先被清掉，再重起。

| 想做的事 | 怎么做 |
|---|---|
| 浏览器预览 + 本地几何服务（推荐） | Run and Debug → **WebGL + .NET Server**（唯一的浏览器配置） |
| 调 .NET 侧（路由 / 会话 / P/Invoke） | Run and Debug → **Geometry Server (attach)** 或 **(launch)** |
| 调原生侧（`tamias_server.dll`、OCCT） | Run and Debug → **Geometry Server (native attach)** |
| 在浏览器里断进 C++ | `cmake --preset wasm-debug`，见[分层调试](DEBUG-BROWSER-STACK.md) |
| 停止 | 关掉 `TamiasWasmServe`（预览）和 `TamiasGeometryServe`（几何服务）两个窗口；或 `powershell -File scripts/wasm-stop.ps1` 一次收掉 |
| 桌面调试 | Run and Debug → **CMake Launch Target**（照旧用 CMake Tools 的 preset） |
| 只构建、不开浏览器 | `powershell -File scripts/wasm-preview.ps1 -NoBrowser` |
| 只构建 wasm（默认，**含 OCCT**，26 MB） | `powershell -File scripts/wasm.ps1` |
| 只构建 wasm（轻量，仅 Truck，2.8 MB 量级） | `cmake --preset wasm-truck` + `cmake --build build/wasm-truck --target tamias_viewer` |
| 只构建 wasm（Debug + DWARF，供浏览器内 C++ 断点） | `cmake --preset wasm-debug` + `cmake --build build/wasm-debug --target tamias_viewer` |
| 编 wasm 用的 OCCT（一次性） | `powershell -File scripts/occt-wasm.ps1` → 默认装到 `C:\dev\occt-wasm\prefix` |
| 实验后端（WebGPU） | `cmake --preset wasm-webgpu` + `cmake --build build/wasm-webgpu --target tamias_viewer` |

F5 那条链路做三件事：编 `build/wasm` 的 `tamias_viewer`（**WebGL2**）→ 起 .NET 几何服务
（`127.0.0.1:5199`，见 [服务端语言决策](DECISION-SERVER-LANGUAGE.md)）→ 起静态预览
（`:3000`）→ 用浏览器调试器打开页面。**调试 WebGL 一律连几何服务一起起**，VS Code 里只有
这一个浏览器配置；确实不要它时走脚本（`scripts/wasm-preview.ps1` 不带
`-WithGeometryServer`）。脚本最后会打印一行 `RHI backend: ...`——
调试 WebGL 时最怕的就是"以为在调 WebGL，其实在跑 WebGPU"，两个产物长得几乎一样。

**四层（TS / wasm / .NET 壳 / 原生 OCCT）各自怎么断、跨层怎么对齐、哪些是静默失败**：
见 [浏览器 wasm + 几何服务的分层调试](DEBUG-BROWSER-STACK.md)。那里也说明了
`wasm-debug` 预设为什么只编 Truck、以及几何回包的 digest 对比工具
（`scripts/geometry-digest.ps1`）怎么用。

命令行（等价于 F5，只是不开浏览器）：

```
powershell -File scripts/wasm-preview.ps1 -NoBrowser
```

CMake Tools 的状态栏里，wasm 只保留一个**构建**预设「构建 WASM 查看器」（Configure
preset 选 `wasm`）——预览统一走 F5，不再有「停止预览」这种预设。想从命令行单独起服务：
`cmake --build build/wasm --target serve_viewer`。产物在 `build/wasm/bin/`。

浏览器里：中键旋转，右键平移，滚轮缩放。进来先是**登录页**，登录后落在**文件管理页**，
双击一份 `.tdoc` / `.trscn` / `.obj` 才进查看器（也可以拖放，或用顶栏「打开」）。**没有默认
文档**——引擎起来时 `has_document()` 是 false，视口提交空帧。`.trscn` 的含义见
[渲染场景快照](RENDER-SCENE.md)。

第一次打开要下 26 MB 的 wasm，所以引擎**在登录页就开始预加载**（带进度条）；如果
`tamias_viewer.wasm` 在 Network 面板里出现了两条请求，那是预加载那条路坏了——正常只有一条。

wasm 构建会自动构建 `web/`（首次若缺少 `node_modules` 会先跑 `npm ci`），
把 Vite 产物 `index.html` + `assets/` 拷到 `build/wasm/bin/`。
只想单独构建 UI：`cd web && npm run build`，然后手动把 `dist/*` 拷到 `build/wasm/bin/`。

不要在 `build/` 里找 `CMakePresets.json`。不要给 wasm 配 vcpkg。

---

## 6. 构建开关

| CMake 选项 | 桌面默认 | WASM 默认 | `wasm` 预设 |
|---|---|---|---|
| `TAMIAS_ENABLE_VULKAN_BACKEND` | ON | OFF | OFF |
| `TAMIAS_ENABLE_OPENGL_BACKEND` | ON | OFF | OFF |
| `TAMIAS_ENABLE_WEBGL_BACKEND` | OFF | **ON**（`EMSCRIPTEN` 时的默认后端） | ON |
| `TAMIAS_ENABLE_WEBGPU_BACKEND` | OFF | OFF | OFF（只由 `wasm-webgpu` 实验预设打开） |
| `TAMIAS_ENABLE_OCCT` | ON | OFF | **ON** |
| `TAMIAS_ENABLE_TRUCK_KERNEL` | OFF | OFF | ON（回退内核） |
| `TAMIAS_OCCT_DATA_EXCHANGE` | ON | OFF | OFF |
| `TAMIAS_ENABLE_IFC` | ON | OFF | OFF |
| `TAMIAS_BUILD_TESTS` | ON | OFF | OFF |

**注意内核这一列**：`TAMIAS_ENABLE_OCCT` 在裸 `emcmake` 下是 OFF，是 **`wasm` 预设**把它
打开的——老版本在 `EMSCRIPTEN` 分支里 `FORCE` 成 OFF，所以预设怎么设都没用（见 §0 与
[建模内核](MODELING-KERNEL.md)）。wasm 上 OCCT 需要一份交叉编译产物（`TAMIAS_OCCT_PREFIX`，
默认 `C:/dev/occt-wasm/prefix`，用 `scripts/occt-wasm.ps1` 生成）。

`TAMIAS_OCCT_DATA_EXCHANGE` 关掉是因为 STEP / IGES / BREP 那条路要 XCAF，而 XCAF 在 OCCT 7.9
上会把 Visualization 一起拖进来；`TAMIAS_ENABLE_IFC` 则从 OCCT 开关上**解耦**了（以前挂在
它上面，于是"wasm 想要 OCCT"会顺带把 Boost 1.87 + IfcOpenShell 的 FetchContent 拉进来）。

`src/app`（Qt 壳）与 IfcOpenShell 在 EMSCRIPTEN 下不进构建。**`src/command` 是进的**——
wasm 链了 `tamias::host`，而 `tamias_host` PUBLIC 链 `tamias::command`，所以 web 端
`dispatch` 走的是和桌面同一套命令层。

---

## 7. 明确不做

- 不把现有 Qt Ribbon / 属性面板编进 WASM（重画在 HTML 上）
- 不挖 OCCT 的**导入**路径（STEP / IGES / BREP）：`TAMIAS_OCCT_DATA_EXCHANGE=OFF`
- 不链 IfcOpenShell / Boost：IFC 仍走桌面，或将来走服务端
- 不上 Vulkan / 桌面 OpenGL 4.5
- 不要求 SharedArrayBuffer
- 不在这一阶段做服务端网格下发（那是另一条可选路径，见 [几何服务方案](https://github.com/terry-chao/tamias/blob/main/GEOMETRY-SERVICE-PLAN.md)）

> 这里**曾经**写着"不在客户端链 OCCT"。那条已经在阶段 3 被推翻了：OCCT 现在就是 wasm 的
> 默认内核（体积代价 2.8 MB → 26 MB，见 §0）。

---

## 8. 下一步

按「先修对不对、再补有没有、最后补交互」排：

1. **渲染正确性**：半透明遍与不透明遍的提交顺序（`draw_channel` 里两个 visitor 的析构顺序反了，
   玻璃会被后画的不透明几何盖掉）；同一个原因还让 HUD 的 `draw` / `tri` 恒为 0（stats 在
   `flush_all()` 之前就发布了）；实例缓冲扩容被拒时静默丢批次。见
   [WebGL 链路审计](WEBGL-PIPELINE-AUDIT.md) §2⑩。
2. **补齐"空着的能力"**：X 光 / 正交 / 轴网显示 / 标注显示的开关。**引擎侧已经有实现**
   （`Camera::set_orthographic()`、`ViewerHost::xray_`），缺 embind 导出 + 顶栏按钮。
3. **把屏幕空间通道喂上数据**：`ViewerHost::render()` 现在只提交天空 / 实体 / 坐标轴。
   `preview_*` / `grip_points` / `snap_point` / `grid_*` / `drawing_overlays` 全是空的；
   文字还缺字体与图集来源（`docs/TEXT.md §3.1` 不入库字体）。WebGL 的 GLSL 管线已就位。
4. **框选**：引擎侧 `nodes_in_screen_rect()` 已成熟，只缺 embind 导出 + HTML 框选矩形。
5. **放置流程的实时预览**：现在只有屏幕空间橡皮筋，桌面端是 `DragContext` 驱动的实体预览。
6. **保存**：`showSaveFilePicker`（Chromium 系），否则 web 永远只是只读查看。要做快照导出还
   得先给 WebGL 补离屏目标 / `read_back_rgba`。
7. **线框**：WebGL / WebGPU 没有 polygon mode，`RenderMode::Wireframe` 现在是"实心三角涂成
   浅蓝"；真线框要么走重心坐标着色，要么给网格补边索引。
8. **健壮性与交互细节**：WebGL context lost 恢复、指针 DPR 单位统一、`wheel` 的 `deltaMode`、
   DPR 变化监听。
9. **大 IFC 走服务端**，或按需加载独立 wasm 模块。
