# WebGL 造型链路审计（实测）

> 调查时间：2026-10-14。方法：**读源码 + 核产物**，不是读计划文档。凡是推断都标了「推断」。
> 关联：[引擎 WASM + Web 查看器](WEB.md)、[WebGL 默认化清单](../WEBGL-EXP-IMPLEMENTATION-PLAN.md)、
> [几何服务方案](../GEOMETRY-SERVICE-PLAN.md)、[建模内核](MODELING-KERNEL.md)、[NDC](NDC.md)。

**一句话结论**：WebGL2 这条线**是通的，而且已经是默认**；浏览器里实际只画
**天空 + 实体 + 三条坐标轴**。真正的缺口是
①**除实体外的所有屏幕空间通道（文字 / 底图 / 预览线 / 夹点 / 捕捉 / 轴网）都没有数据**、
②正交 / X 光已有引擎实现但没导出、
③**半透明遍与不透明遍的提交顺序是反的**（与文档相反，影响所有后端）。

---

## 0. 硬证据（产物级，不是推断）

| 断言 | 证据 | 结果 |
|---|---|---|
| WebGL 真编进产物 | `build/wasm/bin/tamias_viewer.js` 里 `GLctx` **97**、`glDrawElements` **8**、`emscripten_webgl_create_context` **2** | ✅ |
| WebGPU 已出默认 | 同一文件 `wgpu` **0** 命中 | ✅ |
| OCCT 真进 wasm | `.wasm` 里 `OCCT` 6 / `Truck` 9；文件 **26.2 MB**；`C:\dev\occt-wasm\prefix\lib` 有 `libTKMesh.a` 等 18 个包 | ✅ |
| 预设正确 | `build/wasm/CMakeCache.txt`：`WEBGL=ON`、`WEBGPU=OFF`、`OCCT=ON`、`TRUCK=ON`、`VULKAN/OPENGL=OFF` | ✅ |
| 构建是新鲜的 | C++ 产物 mtime 早于最后一次提交，但那次提交（`139f211`）**只动 `web/` 与文档**，无 C++ 源文件变新；`build/wasm/bin/assets` 与 `web/dist/assets` 哈希名一致（`index-BxNoeFOv.js`） | ✅ |

> 也就是说：**不需要重新构建就能验证现有行为**。改 C++ 之后才需要。

---

## 1. 完整链路（逐段已核）

| # | 段 | 落点 |
|---|---|---|
| 1 | DOM canvas（JS 拥有） | `web/src/ViewerApp.tsx:985`（`<canvas id="viewport">`） |
| 2 | 尺寸 = `rect × dpr` | `ViewerApp.tsx:383-392` → `resizeViewer` |
| 3 | 后备存储落地 | `viewer_host.cpp:324-334`（`emscripten_set_canvas_element_size`） |
| 4 | embind 面 | `viewer_main.cpp:123-165`：**34 个函数 + `ViewerStats`** |
| 5 | TS 声明 | `web/src/viewer.ts:12-60`：**34/34 精确对齐**，无多余、无缺失 |
| 6 | 鼠标 / 滚轮 | `viewer_host.cpp:392-420` |
| 7 | 工具目录（43 个） | `web/src/modeling/catalog.ts` |
| 8 | args 文本拼装 | `catalog.ts:71-111`（固定顺序：fixed → fields → selection → points，`;` 分隔，`|` 分隔数组） |
| 9 | args 解析 | `src/host/command_arg_text.cpp:148-221`（类型前缀 `i/d/s/v/a/p`） |
| 10 | 命令层（与桌面同一套） | `session_->dispatch` → `CommandSystem::dispatch` → `execute()` |
| 11 | 特征树求值 | `evaluator.cpp:187-207`：逐特征 `make_rect_face` / `extrude` / `boolean` / `fillet` … |
| 12 | 过渡缝（同步、const） | `geom_builder.h:14-15`、`geom_builder.cpp:10-16` |
| 13 | 内核选择 + 回退 | `linked_kernels.cpp:26-48`，默认 `Occt` |
| 14 | **OCCT 三角化** | `occt_mesh.cpp:44-146`：`BRepMesh_IncrementalMesh` → `Poly_Triangulation` → **Z-up 转 Y-up**（`:137-142`） |
| 15 | 离散队列（wasm 无线程） | `tess_worker.cpp:12-16`（`#if !defined(__EMSCRIPTEN__)` 才建线程） |
| 16 | 逐帧推进 LOD | `viewer_host.cpp:473-494`：每帧 `pump(1)` |
| 17 | 提交 + 上传 | `channel_->submit(frame)` / `upload_mesh` / `upload_texture` |
| 18 | 渲染运行时 | `render_runtime.cpp`：mesh + sky 两条管线走 WebGL |
| 19 | WebGL 设备 | `webgl_device.cpp:284-354` `draw_indexed` |
| 20 | 帧驱动 | JS `requestAnimationFrame` → `renderFrame()`，C++ 不调 `emscripten_set_main_loop` |

后端选择是 **WebGL 优先**（`viewer_host.cpp:171-185`），`RenderThread.synchronous=true`
（`:187`）所以**不需要 SharedArrayBuffer / COOP-COEP**。

### 1.1 已经是对的，别去动

这几处最容易「顺手改坏」，我逐字节核过：

- **`kPushBlock` ≡ `PushConstants`**：`webgl_shaders.h:8-18` 与 `shaders/mesh.hlsli:4-12`
  字段顺序完全一致 → 7 槽 × 16 B = **208 B**，UBO 开在 256 B（`webgl_device.cpp:412`），装得下。
- **顶点/实例属性绑定**：`draw_indexed` 写死的 location 0/1/2/3 = position(12)/normal(12)/uv(8)/color(12)
  与 `Vertex`（stride 48）一致；实例 4-9 与 `GpuInstance`（stride 96，`static_assert(sizeof==96)`）一致。
  → **RHI 层不用动**。
- **Y 轴 / Z 映射**：`clip_space_correction_matrix()`（`webgl_device.cpp:209-214`）只把 Z 从
  `[0,1]` 拉到 `[-1,1]`、**不动 Y**，与 `NDC.md:67-71` 的 OpenGL 惯例一致。
  ⇒ **桌面 GL 分支的 Y 翻转规则对 WebGL 同样适用**（`text.vert.hlsl:32-34`）。
- **预乘 alpha 混合**：`webgl_device.cpp:272` 已是 `ONE / ONE_MINUS_SRC_ALPHA`，与两个待补 shader 的
  输出约定一致。
- **纹理槽位**：`create_pipeline` 绑了 `albedo/normal/irradiance/prefilter/brdf/orm`
  （`webgl_device.cpp:571-606`）；文字与底图都走 `kTextureSlotAlbedo`（`render_runtime.cpp:1432`、`:1455`），
  与 `text.frag.hlsl` / `overlay.frag.hlsl` 一致。

---

## 2. 缺口清单（按收益/成本排）

### ① 文字标注 + 图纸底图：**两层都断了** —— 唯一「功能存在但看不见」的硬伤

**第一层（计划文档记了）**：`webgl_shaders.h` 只有 `mesh_vert/frag` + `sky_vert/frag`，
**没有 text / overlay**；`render_runtime.cpp` 里这两条管线被 `if (!webgl && !webgpu)` 整段跳过
（[`:848`](../src/engine/render/runtime/render_runtime.cpp#L848)、[`:898`](../src/engine/render/runtime/render_runtime.cpp#L898)）。
全文 grep 确认：**这是仅有的两处** web 后端被跳过的地方，mesh/sky 都有 WebGL 分支。

**第二层（计划文档没写，本次新发现，而且比"文字"宽得多）**：`ViewerHost::render()` 只填了
`window / width / height / view / proj / eye_position / view_distance / fovy / orthographic /
mode / xray / items / lod_sets / scene_generation / scene_dirty_ids`。
**下面这些字段一个都没设**：

| 没设的字段 | 后果 |
|---|---|
| `text_quads` / `text_atlas_texture_id` | 文字标注不画 |
| `drawing_overlays` | 图纸底图不画 |
| `preview_polyline` / `preview_control_polyline` / `preview_points` | 绘制预览线、控制多边形、控制点全无 |
| `grip_points` / `snap_point` | **夹点标记、捕捉十字全无** |
| `grid_line_segments` / `grid_preview_segments` / `grid_selected_segments` | 轴网线不画 |
| `debug_line_segments` / `debug_vertex` / `hidden_node_ids` | 调试框、隐藏集无效 |

`ViewerHost` 构造 `FrameSubmission` 的完整代码就是 `viewer_host.cpp:454-471` —— 对照
`render_runtime.h:50-100` 的字段表，上表这些全部缺省。`src/wasm/` 里也**没有任何字体/图集代码**
（grep 只有给文档贴图用的 `upload_texture`）。

> **所以浏览器里实际画的只有**：天空 + 网格地面 + 实体（+ 半透明遍）+ 三条世界坐标轴。
> 坐标轴之所以还在，是因为 `show_axes` 默认 `true`（`render_runtime.h:68`）而 web 宿主从不清它。
> 预览/夹点/捕捉/轴网那几段代码在 web 上是**全部死路**（`render_runtime.cpp:1478-1491` 的
> `has_*` 判断全为 false）。
> 这也是 `docs/WEB.md` 说"只有屏幕空间橡皮筋"的**真正原因** —— 不只是引擎没有 `DragContext`，
> 而是**连屏幕空间的预览线通道都没在喂数据**。

> **结论：光补 GLSL 没用，要改两个地方。** 就算 shader 补齐、管线建起来，浏览器里依然一个标注
> 都不会出现。这也是本次调查最值得记住的一条。

**落地路径**（四步，RHI 层零改动）：
1. `webgl_shaders.h` 加 4 个函数（直译 `text.vert/frag.hlsl`、`overlay.vert/frag.hlsl`，
   注意 GL 分支 Y 翻转：`ndc_y = 1.0 - pixels.y/viewport.y*2.0`）；
2. `render_runtime.cpp:848` / `:898` 的守卫改成按后端分派（照抄 `:688` 的 mesh 写法）；
3. 管道参数**照抄现有值**：text = `instanced=true, depth_test=false, depth_write=false, blend=true`；
   overlay = `instanced=false, depth_test=true, depth_write=false, blend=true`；
4. `ViewerHost::render()` 补 `text_quads` / `text_atlas_texture_id` / `drawing_overlays`
   （需要 web 侧先有文字布局与图集来源；`src/engine/render/text/**` 已存在）。

### ② 正交 / X 光：引擎早就有，只缺导出 —— 最小改动

不是「未实现」，我核过：

- `Camera::set_orthographic()` / `orthographic()` 在 `src/engine/math/camera.h:147-148`，
  投影分支 `:118`；`frame.orthographic` 已接线（`viewer_host.cpp:464`、`render_runtime.cpp:1262`），
  `.trscn` 加载时还会还原（`viewer_host.cpp:270`）。
- `xray_` 成员已有（`viewer_host.h:123`），设置点在 `viewer_host.cpp:466`，`.trscn` 还原在 `:272`。

缺的只是 `setXray(bool)` / `setOrthographic(bool)` 两个 embind 导出 + 顶栏两个按钮。

> ⚠️ `docs/WEB.md:86` 与 `WEBGL-EXP-IMPLEMENTATION-PLAN.md` §5 在这点上**已过时**（都写成「引擎侧还没有对应导出」）。

### ③ 框选：引擎有现成函数，只缺导出

`nodes_in_screen_rect(doc, view_proj, w, h, x0, y0, x1, y1, crossing)` 在
`src/engine/document/picking.cpp:206` **已完整实现**（8 角投影 + crossing 模式），
桌面在用（`document_viewport.cpp:2701`），但**没有 embind 导出** —— 所以 web 端做不了框选。
加一个导出 + 一个 SVG 框选矩形即可，成本远低于计划文档估的「中」。

### ④ 两个导出是死的

`clearSelection`（`viewer.ts:45`）与 `canRedo`（`viewer.ts:42`）声明并实现，但前端**从不调用**
（grep 全 `web/src` 确认）。后果：`Ctrl+Y` 无条件调 `redo()`，重做不可用时是**静默无操作**
而不是灰掉按钮。

### ⑤ 指针 / DPR / 缩放（推断，未运行验证）

canvas 后备存储按 `rect × devicePixelRatio` 设，而指针事件传的是 **CSS 像素**，
`pointerDown/Move/Up` 全程不除 DPR（`ViewerApp.tsx:834-842`）：

- `pickEntity` / `pickWorkPlane` **不受影响**（归一化后比值不变）；
- 但 `orbit/pan` 的位移直接当像素用（`viewer_host.cpp:406-408`）
  → **2× 屏上旋转/平移灵敏度减半**；
- `wheel` 只看 `deltaY` 正负（`viewer_host.cpp:418`）、**不处理 `deltaMode`**
  → Firefox 行模式步长不同；
- **没有 DPR 变化监听**：全 `web/src` 无 `matchMedia` / `resize` / `orientationchange`
  监听，只有 `ResizeObserver(stage)`（`ViewerApp.tsx:394`）——
  而**浏览器缩放（Ctrl+滚轮）改变 DPR 但不改变 CSS 盒子**，`ResizeObserver` 不会触发，
  于是后备存储尺寸与实际像素密度脱钩（画面变糊）。

### ⑥ ViewerHost / 文档状态的既有缺陷（本轮新发现，独立于 WebGL）

这几条与 WebGL 无关，但都在同一条链路上，顺手记下来（都已在源码核过）：

| # | 缺陷 | 位置 |
|---|---|---|
| a | **`xray_` 会跨文档泄漏**：`load_bytes` 开头只重置 `mode_`，**`xray_` 只在 `.trscn` 分支赋值**。于是「先开带 X 光的 `.trscn`，再开 `.tdoc`/`.obj`」→ 新文档**仍是半透明**。`close_document` 倒是清了，所以只有换文档这条路中招 | `viewer_host.cpp:237`（只重置 mode）、`:272`（只有 trscn 赋值）、`:315`（close 才清） |
| b | **`new_document()` 的无核回退分支不打上传**：`load_demo(); return false;` —— 既没调 `upload_document()`，又在文档**其实已打开**时返回 `false`，于是 UI 把 `status_`（`"demo cube"`）当成错误显示出来 | `viewer_host.cpp:151-155`、`load_demo` `:93-98` |
| c | **换文档时离散任务的 `completed_` 队列没清**：`TessWorker::cancel_all()` 只清 pending 队列，**不清 `completed_`**，而 `wait_idle()` 在 wasm 下是 no-op —— 切文档前刚完成的任务会被**新文档**的 `apply_completed_tess_jobs()` 消费，唯一守卫是 `mesh_referenced(id)`，而 mesh/node id 每份 `Document` 都从 1 重新编号（`document.h:240`）→ **有把上一份文档的网格绑进新文档的风险**。（推断：未运行复现） | `tess_worker.cpp:77-82`（take_completed）、`:97-102`（cancel_all 不含 completed）、`document.cpp:467-482` |
| d | **每次点选都重建整份 BVH**：`bvh.build(doc)` 在 `pick_entity` 里，无脏检查/缓存，O(文档) 每次点击；注释承认这是有意的取舍。`nodes_in_screen_rect`（框选）也走同一套投影 | `viewer_host.cpp:366-368` |
| e | **没有事件推送**：`Session::set_listener` 只有桌面视口在用，wasm 宿主从不安装监听 → 命令回显 / `DocumentChanged` / `SelectionChanged` / `ToolChanged` 全被丢掉，web 只能 400 ms 轮询 `stats`/`selectionCount`/`logText`/`viewYaw`/`viewPitch` | `session.cpp:71-75`；`ViewerApp.tsx:434-454` |
| f | **wasm 没有后端探测/回退/安全模式**：`rhi_selection_device`、`probe_rhi`、`run_rhi_startup` 的调用点只有桌面（`src/app/main.cpp:104`、`:129`）。wasm 的后端是**编译期钉死**的，WebGL 建上下文失败就直接 `Err` 给 JS | `viewer_host.cpp:171-185`；`device_factory.cpp:26-38` |
| g | **`TAMIAS_WASM_WEBGL2=1` 是死定义**：`src/wasm/CMakeLists.txt:50` 定义，全仓库无消费者；真正的 WebGL2 约束靠 `-sMIN/MAX_WEBGL_VERSION=2`（`:47-48`） | 同左 |
| h | `clear_log()`、`Session::set_tool()` 都没有导出（C++ 有，JS 拿不到） | `viewer_host.cpp:82-85`、`session.h:39` |
| i | `wheel` 不看光标位置 → **没有「以光标为中心缩放」**（桌面的 `dolly_to_focus` 在 wasm 上没人调） | `viewer_host.cpp:417-420`；`camera_controller.cpp:18-22` |

### ⑦ 陈旧产物陷阱（dev-only，官方链路不受影响）

`web/public/tamias_viewer.{js,wasm}` 是 **2026-09-13 的 2.4 MB Truck-only 旧产物**，被 Vite 复制进 `web/dist/`：

| 路径 | wasm 大小 | 说明 |
|---|---|---|
| `build/wasm/bin/` | **26.2 MB** | 真产物（WebGL + OCCT） |
| `web/public/`、`web/dist/` | 2.4 MB（9-13） | **旧产物**（Truck-only） |

- 官方链路**安全**：`serve-wasm.mjs:9` 默认服务 `build/wasm/bin`；
  `src/wasm/CMakeLists.txt:94-97` 只把 `dist/index.html` + `dist/assets` 拷进 bin，
  刻意**不拷** js/wasm（`:74-77` 有注释说明踩过这个坑）。
- 只有**直接拿 `web/dist` 当站点发布**才会发到旧产物 —— 那种情况下浏览器拿到的是
  2.4 MB / Truck 内核 / 无 WebGL 胶水的版本。
- `npm run dev` 会先跑 `copy-wasm`（`web/package.json:7`）从 `build/wasm/bin` 刷新 `public/`，
  所以这个陈旧副本是**自愈**的；`web/.gitignore` 也已忽略它（不进仓库）。

> 结论：低危，不必单独处理；但**别把 `web/dist` 当发布目录**。

### ⑧ 其他（文档已记，确认属实）

无实时实体预览（引擎侧确实没有 `feed_point` / `DragContext`，只有屏幕空间橡皮筋）；
无夹点编辑；无保存/另存为；`create_beam` 的 T/I 断面、`import_mesh`、轴网轴线 id 拿不到。
另有 9 个命令**已注册但目录里没有**（前端不可达）：`set_location`、`create_curve`、
`set_param`、`copy_storey`、`set_material`、`delete_grid_axis`、`update_text`、`delete_text`、`import_mesh`。

### ⑨ `MeshCpu` 序列化丢字段（全平台，非 web 特有）

`write_mesh_cpu` / `read_mesh_cpu`（`mesh_binary.cpp:97-115` / `:117-153`）只写
**顶点 / 索引 / AABB 三项**：

- **`line_list` 不写** → 草图实体（直线 / 圆弧 / 样条，`curve_geom.cpp:501` 置 `line_list=true`）
  经 `.tdoc` 往返后会丢这个标志，再被当成三角面解释；
- **`has_texcoord` 不写** → 导入网格的 UV 语义丢失；
- **`faces`（BRep 面范围）不写** → 按面拾取 / 面级高亮的信息丢失。

`.trscn` 那条路**没这个问题**：`render_scene_io.cpp:688` / `:853-857` 单独写了 `line_list` 等字段。
所以只影响 `.tdoc`（`document_io.cpp:224` / `:238`）与直接吃 mesh_binary 的路径。

> 这条与 WebGL 无关，是链路中段的数据完整性问题；和 §2① 的文字/底图缺口一样，
> 属于「文档没写、但代码里真实存在」的一类。

### ⑩ 渲染层发现（后端无关，但影响所有后端）

**a. 半透明遍与不透明遍的提交顺序是反的 —— 与文档写的不一致 [已核]**

`RecordCommands::~RecordCommands()` 会 `flush_all()`（`scene_graph.h:221`），而 `draw_channel` 里：

- **不透明** visitor 声明在**外层块**（`render_runtime.cpp:1385`）；
- **半透明** visitor 声明在**内层块**（`:1394`）。

内层块在 `:1396` 结束 → `trans` 先析构 → **半透明批次先发**；`visitor` 要到 `:1406` 才析构
→ **不透明批次后发**。

而 `docs/FAQ.md:365-374` 明确写的是「第一遍不透明、第二遍半透明」（RENDERING.md、
QT3D-VS-TAMIAS.md:74 同口径）。`flush_all()` 自身的顺序是对的（`scene_graph.cpp:484-490`：
先 `flush_batch` 再 `flush_transparent`），**问题只在那两个块的嵌套**。

**后果**：混合管线 `depth_write=false`，所以后发的不透明片元会通过深度测试、**盖掉已经混合好的
玻璃像素** → 玻璃只在"背后没有不透明几何"的地方（例如贴着天空）才看得见。
X 光模式不受影响（X 光下所有面都走半透明，不透明遍本来就空）。

**推断（未运行验证）**：修复就是给不透明 visitor 单独加一层块作用域，让它在 `trans` 之前析构 ——
一行改动。但**别只照文档改代码，先确认文档与代码哪个才是想要的**：现场可能是"按文档"跑了很多
预期，实际一直反着。

**b. 每帧实例缓冲扩容会被静默拒绝 [已核代码 / 严重度推断]**

实例缓冲是每通道 2 槽 ping-pong，初始按 `max((items+4096)*96, 1MiB)*2` 开
（`render_runtime.cpp:1188-1203`）。场景图遍历中可以中途扩容（`:1357-1374`），
但**写入偏移非 0 时拒绝扩容**（`:1361-1363`）。于是「一帧的实例数超过初始容量」时，
`Buffer::write` 会越界（WebGL 是 `glBufferSubData` 越界 → GL 错误），**整个批次被静默丢掉**。

**c. WebGL 没有离屏目标 [已核]**

`create_offscreen_swap_chain` 没有覆写 → 走 `device.h:289-294` 的默认 `Err`。
所以 `resize_offscreen_surface` 只打日志（`render_runtime.cpp:594-596`），
`read_channel_pixels` 直接报错（`:615`），`capture_document_rgba`（`scene_capture.cpp:10-68`）
在 WebGL 上**不可能工作**。桌面 GL 有实现（`opengl_device.cpp:382`、`:1237`）。

**d. 线框在 WebGL 上是"填色"而非线框 [已核，文档也提过]**

GLES 3.0 没有 polygon mode，`WebGLPipeline` **连 `desc.wireframe` 都不存**
（`webgl_device.cpp:93-122`、`:607-608`）。所以 `RenderMode::Wireframe` 在 WebGL 上
是「实心三角 + 片元里涂成浅蓝」（`webgl_shaders.h:197-204`）。
`docs/WGPU.md:61`、`docs/FAQ.md:590` 已记。

**e. sRGB 输出链可疑（推断，建议实测）**

默认 WebGL 绘制缓冲不做 linear→sRGB 编码，ES 3.0 也没有 `GL_FRAMEBUFFER_SRGB`，
而后端从不设置任何 sRGB 写入控制；但 `render_runtime.h:91-94` 假设"渲染目标是 sRGB、
硬件会重编码"，天空 shader 结尾还主动 `srgb_to_linear(...)`（`webgl_shaders.h:300-302`、`:371`）。
⇒ **浏览器画面很可能比 Vulkan 参考偏暗** —— 这与桌面 GL 已记录的同类问题
（`opengl/README.md:600-602`「画面偏暗」）是同一类。**未运行浏览器验证，按推断对待。**

**f. 细节：`ensure_pipelines()` 的早退条件不含 text/overlay**（`:660-667`）——
所以这两条管线一旦创建失败，就**不会再重试**（其余资源齐了就早退）。

**g. 细节：文字图集采样器靠默认落在 0 号槽。** `WebGLDevice::create_pipeline` 只显式绑了
mesh 那六个采样器（`webgl_device.cpp:571-606`），`atlas_tex` / `albedo_tex` 没绑；
好在 GL 默认就是 0，而 `render_runtime.cpp:1455` 正是 `kTextureSlotAlbedo`（= 0）——
**碰巧对**。补 text/overlay 时最好顺手显式 `glUniform1i`，别依赖默认。

---

## 3. 文档 / 注释漂移（会误导后来人）

| 位置 | 问题 |
|---|---|
| `src/wasm/viewer_main.cpp:32` | 注释「OCCT 不在 wasm 构建里，默认后端会退到 Truck」—— **现在是反的** |
| `src/engine/modeling/linked_kernels.cpp:24` | 注释「WASM 只有 Truck」—— 同上过时 |
| `src/wasm/viewer_host.h:5-6` | 头部注释仍写「Truck 建模」 |
| `docs/WEB.md:86` | 「X 光 / 正交 … 引擎侧还没有对应导出」—— 实际引擎侧已有，缺的是导出（细节不同） |
| `docs/WEB.md:144` | 写默认后端是 `GraphicsBackend::WebGL2` —— 枚举其实叫 `WebGL`，只有**显示串**是 `"WebGL2"`（`viewer_host.cpp:173-174`） |
| `WEBGL-EXP-IMPLEMENTATION-PLAN.md` §3 | 只记了 shader 那一层，**漏了 ViewerHost 不产 `text_quads` / `overlays` 这一层** |
| `GEOMETRY-SERVICE-PLAN.md:19` | §0 表格说 wasm 造型后端是 Truck、因为 CMake FORCE 关 OCCT —— **已过时**（`CMakeCache.txt` 是 `OCCT=ON`，那条 FORCE 已删，`CMakeLists.txt:142-145` 有记录）。该方案整体仍是**未落一行代码**（文件自己 `:3` 就写着） |
| `WEBGL-EXP-IMPLEMENTATION-PLAN.md:46`、`:264`、`:272-273` | 行号引用已漂移：真实位置是 `848` / `898`（不是 812/862） |
| `WEBGL-EXP-IMPLEMENTATION-PLAN.md:159` | 说 `kPushBlock` 是 256 B —— **块是 208 B**，256 B 是那个 buffer（见 §1.1） |
| `WEBGL-EXP-IMPLEMENTATION-PLAN.md:352` | 让读者去日志里找 `text shaders unavailable` —— 在 web 后端**永远不会有这条**（那几行 `log_error` 在守卫块**内部**，web 走不进去，是**静默跳过**） |
| `docs/FAQ.md:365-374` | 写「第一遍不透明、第二遍半透明」—— **实际提交顺序是反的**（见 §2⑩a） |
| `docs/RENDERING.md:199` | 写「清单里每一项一次 `draw_indexed`（没有合批）」—— 已过时，`RecordCommands` 按 `BatchKey` 合批（`scene_graph.cpp:359-368`、`:405-482`） |
| `docs/RENDERING.md:196-203` | pass 列表漏了文字遍（`render_runtime.cpp:1442`）与半透明子遍 |
| `docs/TEXT.md:39` | 写「`shaders/` 里 mesh / sky / grid / overlay 四条；没有 `text.*`」—— **两处都错**：实际是 mesh/sky/overlay/**text**（+`mesh.hlsli`），且**没有 `grid.*`** |
| `docs/NDC.md:49-54`、`:67-73` | NDC 表里**没有 WebGL 那一行**，尽管 `webgl_device.cpp:209-214` 与它的 OpenGL 行完全一致 |

### 3.1 一个命名陷阱

`src/engine/modeling/kernel/shape_ops.h`（`Shape` / `IShapeOps`）是**导入路径的内核边界**，
和「服务端几何服务」毫无关系，但它**确实编进了 wasm**
（`build/wasm/src/engine/modeling/kernel/CMakeFiles/tamias_kernel.dir/shape_ops.cpp.o` 存在）。
别把两者当成一回事。

### 3.2 一个 LOD 时序细节

`frame.lod_sets` 在 `viewer_host.cpp:468` 取快照，**早于** `:476` 的 `pump(1)` 与 `:477` 的落地 ——
所以第 N 帧刚离散完的网格，第 N+1 帧才通过 `lod_sets` 可选；这一帧里记录器会落到
「画 LOD 盒 + 再请求一次」的分支（`scene_graph.cpp:211-230`）。不是 bug，是**一帧延迟**，
排查「刚建的构件先显示成盒子」时别误判。

---

## 4. 建议动手顺序

**先修「对不对」，再修「有没有」**——⑩a 和 ⑥c 是正确性问题，其余多是缺功能。

1. **⑩a 半透明/不透明提交顺序**：给不透明 visitor 加一层块作用域即可（`flush_all()` 自身顺序是对的，
   已核 `scene_graph.cpp:484-491`）。**但先确认文档与代码哪个是想要的**——现场可能一直是反的。
2. **⑥c `completed_` 跨文档错绑**、**⑥a `xray_` 泄漏**、**⑥b 回退分支不打上传**：都是小改动的状态缺陷。
3. **②** 正交 / X 光导出 + 按钮 —— 改动最小、立刻可见。
4. **①** 文字与底图**两层**（GLSL 分派 + ViewerHost 补 `text_quads`/`overlays`；顺带补
   `preview_*`/`grip_points`/`snap_point`/`grid_*` —— 现在浏览器里这些通道全是死的）。
5. **③** 框选（引擎侧 `nodes_in_screen_rect` 已成熟）、**④** 接上两个死导出。
6. **⑤** 指针单位统一（DPR）+ wheel `deltaMode` + DPR 变化监听。
7. **⑩b** 实例缓冲扩容被拒的静默丢批次；**⑩c** WebGL 离屏/截图（要做快照导出必须先补）。
8. **⑨** `mesh_binary` 补 `line_list` / `has_texcoord` / `faces`（改格式要留心向后兼容：
   旧文件读不到就是默认值，照 `.trscn` 给 `xray` 留默认值的写法处理）。
9. **⑩e** 实测 sRGB，确认浏览器是否偏暗（推断项，先量再改）。
10. **§3** 注释与文档同步（否则下一个人还会照旧描述找错地方）。

---

## 5. 怎么复验

```powershell
# 产物含 GL 胶水、不含 wgpu
$js='build\wasm\bin\tamias_viewer.js'
$t=[Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($js))
'GLctx','glDrawElements','emscripten_webgl_create_context','wgpu' | % {
  '{0,-34} {1}' -f $_, ([regex]::Matches($t,[regex]::Escape($_))).Count }

# 后端 / 内核开关
Select-String build\wasm\CMakeCache.txt -Pattern 'TAMIAS_ENABLE_(WEBGL|WEBGPU|OCCT|TRUCK)'

# F5 或命令行预览（脚本会打印 "RHI backend: ..."）
powershell -File scripts/wasm-preview.ps1
```

功能面复验：打开示例文档 → 建一段文字标注（注释组）→ **确认视口里没有文字**（这是 ① 的现场）。
注意 `logText()` 面板里**不会有**任何提示：WebGL 走不到那几行 `log_error`（它们在被跳过的守卫块**内部**），
两条管线是**静默不建**的。（`WEBGL-EXP-IMPLEMENTATION-PLAN.md:352` 让你去日志里找这两条，是错的。）
