# UE5 渲染管线 vs Tamias 渲染管线

> 把 UE5（5.0–5.5，桌面默认 Deferred + Nanite + Lumen + VSM + TSR）的管线主线整理成一张图，
> 再逐维度对照 Tamias 现在真正怎么画。Tamias 侧结论对应当前代码，带源码链接；
> UE5 侧是引擎默认行为，**版本 / 平台 / 项目设置都会改变具体 pass**（移动端、Forward+、关闭 Nanite/Lumen 都是另一套）。
> 本项目与 Unreal 没有代码依赖，这是一次**概念对照**。管线本身的细节见 [渲染管线](RENDERING.md)。

---

## 0. 一句话

两者共享同一副骨架——**RHI 抽象 + 独立渲染线程 + 渲染侧场景投影 + 资源缓存 + instancing**；
但 UE5 是「延迟着色 + 动态全局光 + GPU 驱动几何」的通用实时引擎，
Tamias 是「单 pass 前向 + 预烘 IBL + CPU 展平提交」的 CAD/BIM 视口。
差异不在实现细节，而在**要不要历史帧、几何动不动、剔除和提交放在哪一端**。

---

## 1. UE5 渲染管线

### 1.1 分层与线程

```
GameThread（UObject / 组件 / 材质图）
   └─ FScene + FPrimitiveSceneProxy（渲染侧镜像：网格、材质、包围盒、可见性）
        └─ RenderThread（ENQUEUE_RENDER_COMMAND，帧流水线多帧在飞）
             └─ FDeferredShadingSceneRenderer → RDG 建一张 Pass 依赖图
                  └─ RHIThread → RHI（D3D12 / Vulkan / Metal …）
                       └─ Driver → GPU
```

和 Tamias 一样，语义侧（游戏线程 / 文档）绝不直接画图：渲染侧有一份自己的镜像，
提交通过命令队列跨线程传递，GPU 资源只在持有设备的那条线程上创建。

### 1.2 帧内阶段

```
PrePass（深度 / 速度，可选）
  → BasePass：写 GBuffer（法线、albedo、金属/粗糙、速度…），或 Forward / Forward+
  → Nanite：cluster 层级 + GPU 驱动 HiZ 遮挡剔除 + 软/硬光栅 + Visibility Buffer → Material Resolve
  → 阴影：Virtual Shadow Map（页表缓存）+ CSM / 接触阴影
  → 光照：延迟光照 pass（clustered 光源列表）；半透明走 forward clustered
  → GI / 反射：Lumen（SDF 软光追或硬件 RT + surface cache + radiance cache + 屏幕空间追踪）、SSR、反射捕获
  → 半透明 / 贴花 / 体积雾 / 粒子等各自 pass
  → 后处理链：运动模糊、DOF、Bloom、自动曝光（直方图）、TSR/TAA 上采样、tonemap、调色
  → Present
```

### 1.3 四个标志性机制

| 机制 | 解决什么 | 代价 |
|---|---|---|
| **RDG（Render Dependency Graph）** | 一帧描述成 pass 的 DAG，资源生命周期、transient 别名、barrier 自动推导，还能合并/裁剪 pass | pass 数量大，调试要理解图 |
| **GPU 驱动** | GPUScene 把图元数据放进 structured buffer，FMeshDrawCommand 缓存提交，视锥/遮挡/实例剔除放 GPU，indirect draw 提交 | 需要持久化 GPU 数据结构 |
| **Nanite** | 几何虚拟化成 cluster 层级，连续细节取代 LOD，几乎没有 per-object draw | SM6 + D3D12/Vulkan，材质受限 |
| **时序累积** | Lumen、VSM 缓存、TSR 都靠历史帧降噪/放大 | 必须有运动矢量，且对历史残留敏感 |

### 1.4 可选项与平台变体

Deferred 是桌面默认，但引擎里同时躺着 Forward、Forward+、Mobile、Instanced Stereo 等变体；
Nanite / Lumen / VSM 都要 SM6 + D3D12/Vulkan 才能开。**谈 UE5 管线必须先说清楚是哪个平台和哪套开关。**

---

## 2. Tamias 渲染管线

### 2.1 分工与数据流

```
特征树 / STEP / OBJ →（tessellate）→ MeshCpu ──upload──▶ GpuMesh（显存缓存，LRU 驱逐）
                                                              │
Scene（语义树，层级唯一真相源）── render_items() 展平 + 视锥剔除 ──▶ SceneDrawItem[]
                                                              │
DocumentViewport：填 FrameSubmission（窗口/相机/清单/模式）── mailbox 提交
                                                              ▼
RenderThread（每后端配置一条线程，多渠道）
  ├─ 留存渲染场景图：Group/Transform/StateGroup/Drawable，按 generation + dirty_ids 增量同步
  ├─ RecordCommands 访问者：累积世界矩阵 → 状态命令 → 按 BatchKey 分桶 → 写 GpuInstance → instance_count = N
  └─ 单 render pass（直接画到 swapchain）
                                                              ▼
RHI：桌面 Vulkan（主）/ OpenGL，浏览器 WebGPU / WebGL；DXC 把 HLSL 编成两份 SPIR-V
```

五段分工、场景图展平与视锥剔除见 [渲染管线](RENDERING.md)、[场景图](SCENE-GRAPH.md)、
[视锥剔除](FRUSTUM-CULLING.md)。

### 2.2 一帧的绘制顺序（代码事实）

`RenderThread::draw_channel()` 是整套渲染的心脏（
[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp)，
顺序即图层，深度测试再挡一层）：

```
清屏
  → 背景：全屏三角画天空渐变 + 地面 + 工作平面网格（不写深度）
  → 不透明模型：留存场景图 + RecordCommands 录制，按 BatchKey 实例化提交
       LOD：Box / Coarse / Work，按投影像素 + 迟滞切换（选中/线条强制 Work）
  → 半透明第二遍：材质 opacity < 1 或开了 X 光（X-Ray），远到近排序 + 相邻同键合并
  → 参考图纸底图：测深度、不写深度
  → 屏幕空间文字：字形图集一次实例化 draw
  → 世界坐标轴（不测深度）
  → 预览线 / 轴网 / 夹点 / 捕捉标记（不测深度）
  → present
```

### 2.3 着色、材质、几何

- **前向着色**：每个 draw 在 fragment shader 里一次算完。`mode` 0/1/2 = 线框 / Lambert 构件色 / metallic-roughness PBR（GGX 方向光 + CPU 预烘 split-sum 工作室 IBL）。
- **材质**：固定 glTF 式 metallic-roughness（albedo / normal / ORM + 标量），BRep 用 triplanar，导入网格带 UV 则走 UV。
- **几何**：BRep 按 deflection 离散出多档 LOD，几何 intern（一份网格 N 个引用），改参数走 copy-on-write 重上传。
- **半透明**：单一 premultiplied alpha 混合；X 光是视图级 alpha，与显示模式正交。

### 2.4 RHI 的能力边界

[rhi/device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)
的抽象只有 vertex/fragment、单颜色目标、固定纹理槽、push constants；
**没有 compute、UAV、多渲染目标、bindless、显式 barrier**。
后端的 NDC 差异（Vulkan Y 向下 / Z [0,1]）由 `clip_space_correction_matrix()` 与两份 shader 变体吸收，
绘制代码里没有 `#ifdef`。离屏通道（`create_offscreen_swap_chain` + `read_back_rgba`）让导出图片和金样测试
复用同一条绘制路径。

---

## 3. 逐维度对照

| 维度 | UE5 | Tamias |
|---|---|---|
| 定位 | 通用实时引擎，追求动态、电影级、跨平台 | CAD/BIM 视口，追求「看得准、认得出构件」 |
| RHI | D3D12/Vulkan/Metal…，含 compute、UAV、多 RT、bindless、barrier | 自研小 RHI：只有 vertex/fragment、单颜色目标、固定纹理槽、push constants；无 compute |
| 渲染侧场景 | FScene + PrimitiveSceneProxy + GPUScene / MeshDrawCommand 缓存 | VSG 式渲染场景图（Group/Transform/StateGroup/Drawable），语义 `Scene` 仍是唯一真相源 |
| 线程 | GameThread → RenderThread → RHIThread，帧流水线 | UI 线程 → 单条渲染线程（每后端配置），mailbox 丢中间帧，上传是阻塞任务 |
| 帧组织 | RDG Pass 图，几十~上百 pass、多个中间 RT | 单 render pass + 固定绘制顺序，直接写 swapchain，没有中间 RT |
| 着色路径 | Deferred BasePass → GBuffer → 光照 pass（或 Forward+） | 纯 Forward，每 draw 在 fragment 里一次算完光照 |
| 光照 / 阴影 | 延迟光照、clustered、VSM/CSM、接触阴影 | 一盏固定方向光 + 预烘 IBL；**没有阴影** |
| GI / 反射 | Lumen、SSR、反射捕获、天空大气 | CPU 预烘 split-sum 环境立方体（irradiance / prefilter / BRDF LUT）；无动态 GI |
| 几何 | Nanite 虚拟几何 + 自动 LOD + HLOD | BRep 按 deflection 离散出 Box/Coarse/Work/Close 四档 LOD + 几何 intern |
| 可见性剔除 | GPU 视锥 + HiZ 遮挡 + Nanite cluster 剔除，indirect 提交 | CPU 按叶子世界 AABB 视锥剔除（一期）；LOD 按投影像素 + 迟滞；无遮挡剔除 |
| 绘制提交 | MeshDrawCommand + instancing + MDI/indirect，GPU 驱动 | 几何 intern + GPU instancing（BatchKey 分桶，`instance_count = N`）；**无 MDI / indirect** |
| 透明 | 独立 translucency pass，per-object 排序 + OIT 选项，半透明走 forward 光照 | 第二遍半透明，远到近排序 + 相邻同键合并，premultiplied alpha；X 光是视图级 alpha；无 OIT |
| 后处理 / AA | TSR/TAAU、TAA、Bloom、DOF、运动模糊、自动曝光、tonemap、调色 | **没有后处理链**：exposure 固定 1.0，无 tonemap / Bloom / AA，靠硬件 sRGB 写回 |
| 材质 | 材质图编译成 HLSL，参数与特性极其丰富 | 固定 glTF 式 metallic-roughness：albedo / normal / ORM + 标量，预设材质，triplanar 兜底 |
| 后端 / 平台 | 桌面、主机、移动、VR 多套管线变体 | 桌面 Vulkan/OpenGL + 浏览器 WebGPU/WebGL；HLSL 编两份 SPIR-V |
| 离屏 | 大量 RT 与历史缓冲 | 离屏 swapchain + `read_back_rgba()`，给 `--render-view` 导出图片和金样测试用 |

---

## 4. 相同的骨架

1. **都有一层 RHI** 把后端差异挡在绘制代码之外。Tamias 的 `draw_channel` 里没有 `#ifdef VULKAN`，等价于 UE 的 RHI 抽象目标。
2. **都做「语义侧 ↔ 渲染侧」分离**：UE 是 UObject/组件 ↔ FScene/Proxy，Tamias 是 `Scene` ↔ `SceneDrawItem` + RenderNode 场景图。
3. **都是多线程提交**：渲染线程独立于逻辑/UI 线程，GPU 资源只能在持设备线程创建，与 UE 的 enqueue render command 同构。
4. **都缓存 GPU 资源 + 用 instancing**：UE 的 MeshDrawCommand 对应 Tamias 的 BatchKey 分桶 + `GpuInstance`。
5. **都做视锥剔除、LOD、预烘 IBL**，都用 HLSL 写 shader 预编译成后端二进制，都靠 per-draw 常量传参。

---

## 5. 为什么不同

差异不在实现细节，而在三件事：

| 分水岭 | UE5 的选择 | Tamias 的选择 |
|---|---|---|
| **时间性 vs 单帧正确性** | 大量依赖历史帧（TSR、Lumen 时序去噪、运动矢量、自动曝光） | 不做时序：点击、测量、剖切不能有历史残留，也不做浮点抖动的 TAA |
| **动态 vs 静态** | Nanite/Lumen/VSM 都在解决「几何和光照每帧都可能变」 | BRep 只在改参数时重算、光照是预烘 IBL，所以「CPU 离散 + intern + 一次性上传」划算 |
| **GPU 驱动 vs CPU 驱动** | 剔除和提交压到 GPU 换扩展性 | CPU 展平 + 分桶 + 一次 `draw_indexed(N)`，受 CPU 提交与 draw 数限制 |

最后一个分水岭直接决定性能天花板：一万根**仍在画面里**的同型号柱，
在 Tamias 里是几次 instance draw（合批已经落地），但不同几何的墙仍是一次一份提交；
UE 用 GPU 剔除 + indirect 把这段也吃掉了。见 [合批 / Instancing](INSTANCING.md) 与 [超大规模三角](MASSIVE-GEOMETRY.md)。

---

## 6. 从 UE5 该借鉴什么

值得借鉴的不是 Deferred / Lumen / Nanite，而是**可见性与提交策略**：
GPU 视锥 + HiZ 遮挡、mesh draw command 缓存、indirect / MDI。

而对 CAD 更契合的路线，反而是 UE 里的「非默认」分支：Forward+ 光照、不透明/半透明分离、
OIT / 深度剥离——以及 UE 并不提供的 Hidden Line、剖切、构件识别色。
阴影、AO、动态 HDRI、OIT 目前是**刻意没做**，不是漏画，见 [渲染管线 §11](RENDERING.md) 与 [路线图](ROADMAP.md)。

一个具体的判断标准：

| UE5 特性 | 对 Tamias |
|---|---|
| GPU 剔除 / indirect 提交 | **值得做**：直接抬高可见几何的规模上限 |
| MeshDrawCommand 式提交缓存 | **值得做**：和现有 BatchKey 分桶同源 |
| Forward+ / clustered 光照 | **可借鉴**：多光源而不引入 GBuffer |
| OIT / 深度剥离 | **值得做**：解决当前半透明的排序近似 |
| Deferred + GBuffer | 暂不需要：没有多光照/后处理需求，白付带宽和 RT 内存 |
| Nanite 虚拟几何 | 暂不需要：几何在 CPU 侧离散、变化频率低，且要保留精确 BRep |
| Lumen / VSM / TSR | 暂不需要：都建立在时序与 GPU 驱动之上，与 CAD 的单帧正确性冲突 |

---

## 7. 源码地图

| 主题 | 文件 |
|---|---|
| 一帧的绘制顺序、上传、channel | [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.cpp) / [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_runtime.h) |
| 渲染侧场景图、RecordCommands、BatchKey | [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene/scene_graph.h) / [batch_key.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/batch_key.h) |
| 实例记录布局 | [gpu_instance.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/gpu_instance.h) |
| LOD 选择 | [mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/resource/mesh_lod.h) |
| draw item / push constants | [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/runtime/render_types.h) |
| RHI 抽象 | [rhi/device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h) |

---

## 延伸阅读

- [渲染管线与 RHI](RENDERING.md)：Tamias 完整版（含 shader、材质、总图）
- [场景图](SCENE-GRAPH.md)：语义树与渲染侧投影
- [视锥剔除](FRUSTUM-CULLING.md)：屏外不发 draw
- [合批 / Instancing](INSTANCING.md)：G1 的 L0/L1/L2 三层
- [超大规模三角](MASSIVE-GEOMETRY.md)：几十亿三角的路线
- [路线图](ROADMAP.md)：明确没做的与打算做的
