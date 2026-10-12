# Qt3D 对照：框架式场景图 vs Tamias 的投影式渲染

> 把 Qt 6 的 Qt3D（Core 的实体 / 组件 + Aspect 作业系统 + Render aspect 的 FrameGraph）
> 整理成一张图，再逐维度对照 Tamias 现在真正怎么画。Tamias 侧结论对应当前代码，带源码链接；
> Qt3D 侧是它公开的设计形状，**Qt 版本与后端会改变具体行为**（Qt 5 是 OpenGL 后端，
> Qt 6 起渲染走 RHI 插件；默认帧图由 Qt3DExtras 提供）。
> 本项目与 Qt3D **没有代码依赖**，这是一次**概念对照**，不是迁移计划。
>
> 本文只谈渲染侧的场景图与渲染器。**界面控件那一刀（Qt Widgets → tac / TacUI）不在范围里**，
> 见 [界面层（tac / Qt）](TAC.md)；两把刀的区别见 §6。

---

## 0. 一句话

两者共享「一层 RHI 抽象 + 一条独立渲染线程 + 渲染侧自有结构」这副骨架，
但 Qt3D 是**留存式、声明式、通用**的框架：应用往实体树里塞组件，渲染流程由 FrameGraph 描述，
框架自己维护前端 / 后端两棵树；Tamias 是**投影式、固定管线、专用**的视口：
真相在 `Document`，渲染侧只收一份绘制投影，pass 顺序写死在代码里。

差异不在实现细节，而在三件事：**谁持有场景、渲染流程是数据还是代码、要不要给第三方留扩展点。**

---

## 1. Qt3D 的形状

```
应用代码：QEntity 树 + QComponent（Qt3DCore）        ← 前端树，Qt 主线程
   └─ 属性变更排进队列，由 change arbiter 投递
        └─ 后端树：QBackendNode 镜像 + Aspect 作业系统（线程池跑 job 图）
             ├─ Render aspect：遍历 FrameGraph → RenderEntity + 绘制命令 → RHI
             ├─ Input / Logic / Animation aspect ……（各自独立作业图，可不用）
             └─ Qt 6：RHI 插件 → Vulkan / D3D11 / D3D12 / Metal / OpenGL
```

三个决定形状的设计：

1. **前端 / 后端双树。** 应用碰的是前端实体树，渲染读的是后端镜像，属性变更异步流过去。
   这笔架构成本换来的是：API 通用、QML 里能直接用、第三方能写自己的组件与 aspect。
2. **Aspect 是插件，作业是图。** 一个组件声明自己属于哪些 aspect；aspect 把这一帧的活
   拆成有依赖关系的 job，丢到线程池并行执行。
3. **FrameGraph：渲染流程是数据。** `QViewport` / `QCameraSelector` / `QClearBuffers` /
   `QRenderTargetSelector` / `QRenderPassFilter` / `QTechniqueFilter` / `QSortPolicy` /
   `QDispatchCompute` 这些节点组成一棵树，**树的形状就是管线**：多视口、离屏链、后处理、
   选择性 pass 都在树上搭。实体侧的 `QMaterial`（`QEffect` → `QTechnique` → `QRenderPass`
   → `QShaderProgram`）按 filter key 与帧图路径匹配。

几何 / 材质同样走通用路线：`QGeometryRenderer` + `QGeometry`（`QAttribute` / `QBuffer`）
描述任意网格，实例化靠 `QAttribute` 的 divisor，文件装载交给场景装载器，
拾取是 `QObjectPicker` / `QRayCaster` 一类组件加 `QPickingSettings`。

> 版本现状（会随 Qt 版本变，不是本文的判断依据）：Qt 6 的 3D 主力是 Qt Quick 3D，
> Qt 3D 以维护为主。挑参照物时，「参照物自己的活跃度」也该算进选型。

---

## 2. Tamias 的形状

```
特征树 / STEP / OBJ →（tessellate）→ MeshCpu ──upload──▶ GpuMesh（显存缓存）
Scene（语义树，层级唯一真相源）── render_items() 展平 + 视锥剔除 ──▶ SceneDrawItem[]
DocumentViewport：填 FrameSubmission（窗口/相机/清单/模式 + scene_generation + dirty_ids）
   └─ mailbox 提交（只保留最新一帧，中间帧丢掉）
        └─ RenderThread：留存 VSG 式绘制投影 + RecordCommands 访问者 + 单 render pass
             └─ RHI：桌面 Vulkan（主）/ OpenGL，浏览器 WebGPU / WebGL
```

四条代码事实：

- **语义树是唯一真相源**，渲染侧不复制它的层级；展平与术语澄清见 [场景图](SCENE-GRAPH.md)。
- **渲染侧留存的是绘制投影**：`Group` / `Transform` / `StateGroup` / `Drawable` 节点 +
  `StateCommand` 命令图，`build_scene_graph` 全量兜底、`update_scene_graph` 按
  `dirty_ids` 增量更新（[scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene/scene_graph.h)）。
- **pass 顺序是代码**：`RenderThread::draw_channel()` 里写死的背景 → 不透明 → 半透明 →
  overlay → 文字（[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp)）。
- **RHI 的能力面是刻意窄的**：单颜色目标、固定纹理槽、push constants，没有 compute / 多渲染目标 /
  bindless（[rhi/device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)）。

点选不在渲染侧：视口用 CPU 上的物体级 BVH 对三角网打射线，命中后改 `SceneNode.selected`，
下一帧清单里带 `selected=true`（[picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h)）。

---

## 3. 逐维度对照

| 维度 | Qt3D | Tamias |
|---|---|---|
| 场景的所有权 | 框架持有留存实体树，应用把它当真相用 | 语义树 `Scene` 是唯一真相源，渲染侧只是绘制投影 |
| 前端 / 后端 | 双树 + 变更仲裁，属性异步同步到后端 | 一棵树；跨线程只传值（`FrameSubmission` + 脏 id） |
| 渲染侧结构 | Render aspect 自建 RenderEntity + 绘制命令 | VSG 式节点树 + 命令图状态，按代次增量同步 |
| 帧的结构 | 声明式 FrameGraph，pass 由树的形状决定 | 固定顺序，写死在 `draw_channel` 里，单 render pass |
| 多视口 / 离屏 | 帧图里再挂一个 surface selector 就是第二个视口 | 一个视口一条 channel + 一条渲染线程；离屏走单独的 offscreen swapchain |
| 材质 | `QMaterial`/`QEffect`/`QTechnique` + filter key，可换着色器 | 固定 glTF metallic-roughness 预设，HLSL 预编译成多份后端产物 |
| 几何 | 通用 `QGeometry`/`QAttribute`/`QBuffer` + `instanceCount` | `MeshCpu`/`GpuMesh` + 几何 intern + 四档 LOD（deflection 离散） |
| LOD | `QLevelOfDetail`，按阈值切换 | `RecordCommands` 按投影像素选档 + 迟滞；**LOD 不是场景图节点** |
| 拾取 | 组件（`QObjectPicker` / `QRayCaster`）+ 后端 BVH | CPU 物体级 BVH，视口直接打射线 |
| RHI 能力面 | = QRhi（多渲染目标、compute dispatch 等） | 自研小 RHI：单颜色目标、固定纹理槽、push constants；无 compute / MRT / bindless |
| 扩展点 | 写 aspect / 组件 / 自定义帧图，第三方可扩 | 改引擎代码；插件只有命令与实体 API |
| 线程模型 | 各 aspect 的作业图跑在线程池 | UI 线程 + 每后端一条渲染线程，mailbox 丢中间帧 |
| 与 Qt 的关系 | 绑定 Qt 运行时与 `QWindow` | engine 无 Qt，Qt 只管窗口与控件，可 headless / wasm |

---

## 4. 相同的骨架

1. **都有一层 RHI**：Qt3D 靠 QRhi 插件，Tamias 靠自研 `rhi/device.h`。目标一致——
   绘制代码里不出现后端 `#ifdef`。
2. **都做「语义侧 ↔ 渲染侧」分离**：Qt3D 是前端实体树 ↔ 后端 RenderEntity，
   Tamias 是 `Scene` ↔ `SceneDrawItem` + RenderNode 绘制投影。
3. **都是渲染独立于 UI 线程**，GPU 资源只在持设备的那条线程上创建。
4. **都缓存 GPU 资源**：Qt3D 由 Render aspect 管理几何与纹理，Tamias 是 `GpuMesh` 留存 +
   几何 intern + `BatchKey` 分桶实例化。

---

## 5. 三处分水岭

### 5.1 留存双树 vs 投影

Qt3D 必须在架构上付「两棵树 + 异步变更」这笔账，因为它的实体树是**给应用用的一等公民**。
Tamias 反过来：真相在 `Document`，渲染侧拿到的是「这一帧要画什么」，跨线程传的是**值和 id**，
不是共享可变对象。于是不存在「属性同步到渲染了吗」这个问题——撤销、测量、剖切永远是单帧精确的。
代价是渲染侧不可能变成通用 API，一切都从引擎内部长出来。

### 5.2 声明式帧图 vs 固定管线

FrameGraph 是 Qt3D 最像「操作系统」的部分：把 pass 顺序从代码提成数据，多视口、离屏链、
后处理、选择性 pass 都不用改 C++。Tamias 的 pass 顺序写死，想加一条就改代码加 shader。
换回来的是零配置、可预测，以及**离屏导出与金样测试复用同一条绘制路径**
（`create_offscreen_swap_chain` + `read_back_rgba`，见 [渲染场景快照](RENDER-SCENE.md)）。

### 5.3 通用框架 vs 专用视口

Qt3D 给的是「任意几何 + 任意材质 + 任意帧图」的积木；Tamias 给的是 CAD/BIM 构件的专用通路：
triplanar 兜底、deflection 驱动的 LOD、几何 intern、BIM 可见性、剖切、测量。
从没打算让用户可编程渲染器——需要的是「点中的是这面墙」和「这个尺寸准不准」。

> 注意：**别把这条差异错记成「时序性」**。两边都不是历史帧渲染器（都没有 TAA / 时序降噪）。
> 真正的分水岭是留存 / 声明式 vs 展平 / 固定，不是「要不要历史帧」——
> [UE5 对照](UE5-VS-TAMIAS.md) 里那条轴放到 Qt3D 上不成立。

---

## 6. 别把两把刀混

| | tac / TacUI | Qt3D 所在的那层 |
|---|---|---|
| 换的是什么 | **Qt Widgets**：窗口、控件、对话框、主题、文本度量 | 场景图 + 渲染器 |
| 现状 | 正在用，抽象契约已落地（[界面层（tac / Qt）](TAC.md)） | **从来没用过 Qt**：自研 RHI + 自研渲染线程 |
| 类型 | 替换一个正在用的 Qt 模块 | 从未采用，自己做了一份职能相当、形态不同的东西 |

这也解释了目录形状：`src/app` 里一百多个文件含 Qt 头（门禁棘轮只减不增，
见 [scripts/check-tac-boundary.ps1](https://github.com/terry-chao/tamias/blob/main/scripts/check-tac-boundary.ps1)），
而 `src/engine` 一行 Qt 都没有——**渲染那一层早在 tac 之前就与 Qt 无关了**。

---

## 7. 源码地图

| 主题 | 文件 |
|---|---|
| 语义树、展平 | [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) / [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) |
| 绘制投影（VSG 式节点 + 命令图 + 增量同步） | [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene/scene_graph.h) |
| 帧提交、单 pass 绘制顺序、mailbox | [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h) / [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp) |
| draw item / push constants | [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_types.h) |
| RHI 抽象（能力边界） | [rhi/device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h) |
| 点选 BVH | [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) |
| 界面抽象层（另一把刀） | [src/ui/tac](https://github.com/terry-chao/tamias/tree/main/src/ui/tac) / [src/ui/qt](https://github.com/terry-chao/tamias/tree/main/src/ui/qt) |

---

## 延伸阅读

- [渲染管线与 RHI](RENDERING.md)：Tamias 完整版（含 shader、材质、总图）
- [场景图](SCENE-GRAPH.md)：语义树 / 场景图 / 展平列表三重歧义，以及与 OCCT 的对照
- [UE5 对照](UE5-VS-TAMIAS.md)：另一个极端——GPU 驱动 + 时序累积
- [界面层（tac / Qt）](TAC.md)：界面控件那一刀的目标形态与迁移阶段
- [合批 / Instancing](INSTANCING.md) 与 [超大规模三角](MASSIVE-GEOMETRY.md)：CPU 驱动的提交策略能走多远
- [空间索引（点选 / 框选）](SPATIAL-INDEX.md)：拾取的结构，不是场景图
