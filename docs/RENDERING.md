# Tamias 渲染是怎么实现的

> 从「屏幕上那张图从哪来」讲到 Tamias 现在真正怎么画。所有结论对应当前代码。几何从哪来见 [特征树求值器](FEATURE-TREE-EVALUATOR.md)；语义树和展平见 [场景图](SCENE-GRAPH.md)；三角怎么进视锥、NDC、深度缓冲见 [视锥、NDC 与屏幕](NDC.md)；屏外不发 draw 见 [视锥剔除](FRUSTUM-CULLING.md)（一期已落地）。OpenGL 怎么绑缓冲、画三角、绑贴图见 [OpenGL 后端](OPENGL.md)。第三后端方案见 [wgpu 接入](WGPU.md)（代码未落地）。

---

## 0. 先建立直觉

显示器不会画「盒子」「墙」「BRep」。它只会给每个像素一个颜色。

所以整套渲染干的事只有一句：

**把三维里的三角积木，经过相机投影，涂上颜色，写进窗口的每一帧。**

投影不是把盒子摊成一张纸：视锥是金字塔，NDC 是压成的立方体；xy 上屏，z 进深度缓冲。展开见 [视锥、NDC 与屏幕](NDC.md)。

Tamias 把这件事拆成五段，互不越界：

| 谁 | 干什么 | 不懂渲染时可以想成 |
|---|---|---|
| 特征树 / 导入 | 做出三角网（CPU 上的积木） | 做道具 |
| 语义树 `Scene` | 记住谁在哪、什么颜色、选中没 | 演员表和站位 |
| 视口 `DocumentViewport` | 每帧打包「这一镜要画什么」 | 导演喊 action |
| 渲染线程 `RenderThread` | 真去 GPU 上画 | 摄影棚 |
| RHI（Vulkan / OpenGL） | 把「画」翻译成两种 GPU 方言 | 两套摄影机说明书 |

中间那张快递单叫 `SceneDrawItem`：语义侧填好，渲染侧照着画，**渲染侧不知道「这是一面墙」**。

---

## 1. 三角网：GPU 唯一认得的形状

CAD 内核里的精确实体是 [BRep](FEATURE-TREE-EVALUATOR.md)（曲面方程）。GPU 不会解方程，只吃**三角形**：

```
顶点 Vertex：位置 + 法线 + UV + 颜色
索引 indices：每三个下标组成一个三角形
```

这份东西在 CPU 内存里叫 `MeshCpu`（[mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h)）。来源有两条：

1. **自己建的实体**：特征树求值 → BRep → tessellate → `MeshCpu`（改参数会整份重做）。
2. **导入**：OBJ / GLB 直接是三角；STEP 由 OCCT 读成 BRep 再离散。

上传到显存之后变成 `GpuMesh`：一块顶点缓冲 + 一块索引缓冲。语义侧继续用 `mesh_asset_id` 指「第几号积木」；渲染侧用表 `asset_to_gpu_` 翻成 GPU 句柄。

> **半留存**：积木（GPU 网格）留在显存里，不每帧重传。场景结构（谁在哪、什么颜色）**每帧重新交一份清单**，不在 GPU 上建一棵树。

---

## 2. 从「场景」到「要画的清单」

语义树 `Scene` 管父子和变换；「墙属于哪一层」由 [BIM 业务层](BIM.md) 写入 `parent`。渲染不要这棵树，只要叶子。

[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) 的 `Document::render_items()` 做展平：

1. 遍历每个 `SceneNode`。
2. 没有网格的分组节点跳过。
3. 若传入视锥且世界包围盒完全在外，跳过。
4. 带上已经算好的**世界变换**（父链乘过了，不是局部坐标）。
5. 若节点对应实体且有材质，填上 `base_color` / 粗糙度 / 金属度 / 贴图 id。
6. 带上 `selected`。

产出一列 `SceneDrawItem`（[render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h)）。到这里，「楼层」信息已经烤没了，只剩「这块网格放在这个世界矩阵上，用这个颜色画」。视口传入视锥时，世界包围盒完全在镜头外的叶子不会进清单，见 [视锥剔除](FRUSTUM-CULLING.md)。

---

## 3. 视口：UI 线程怎么喊「画一帧」

每个打开的文档有一个 `DocumentViewport`（Qt 窗口 + 原生 HWND）。鼠标转相机、点选、改参数都在这条线程。

真正提交在 `submit_current_frame()`（[document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp)）：

1. 向线程池要一条 `RenderThread`（设置里选 Vulkan 或 OpenGL）。
2. 为这个视口开一个 **channel**（一条独立的「银幕」：窗口句柄 + 宽高）。
3. 填一张 `FrameSubmission` 快递单：
   - 窗口、宽高
   - 相机的 view / proj、眼睛位置
   - 显示模式（线框 / 着色 / 真实）
   - `document_->render_items(&frustum)` 那份清单（屏外叶子已丢掉）
   - 要不要画坐标轴、拖墙时的预览线
4. `channel_->submit(frame)`：只覆盖「最新一帧」，渲染线程来不及画的中间帧会丢掉（游戏里常见的 mailbox）。

相机是 **Y-up 转盘相机**（[camera.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/camera.h)）：绕目标点转 yaw/pitch，和 Blender / glTF 一致。左键旋转、中键平移、滚轮推拉。ViewCube 只改 yaw/pitch，不换渲染路径。

点选**不是** GPU 做的。视口用 CPU 上的 BVH 对三角网打射线（[picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h)），命中后改 `SceneNode.selected`，下一帧清单里 `selected=true`，shader 把颜色偏橙。

改参数或撤销后，视口调用 `resync_all_meshes()`：把新的 `MeshCpu` 再 `upload_mesh` 一次，覆盖同一 `asset_id` 对应的 GPU 缓冲。

---

## 4. 渲染线程：摄影棚里发生什么

`RenderThread`（[render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h)）是**一条专门跟 GPU 说话的线程**。UI 绝不能直接调 Vulkan/OpenGL 画图，否则和 Qt 抢消息循环、也和 GPU 驱动抢队列。

线程循环（`thread_main`）大致是：

```
等通知（有任务，或某个 channel 有最新帧）
先执行任务（上传网格/纹理、销毁 channel）
再对每个有新帧的 channel 调 draw_channel()
```

上传必须进任务队列：创建 buffer / texture 只能在持有 `RHIDevice` 的那条线程上做。`upload_mesh` / `upload_texture` 会阻塞 UI 直到 GPU 侧写完，保证下一帧能用到新数据。

**线程怎么共用：**

- Vulkan：相同后端 + 相同校验设置的视口可以共用一条 `RenderThread`（一个 Device 多个 channel）。
- OpenGL：**从不共用**。每个 GL 视口自己的上下文，和别的窗口共享会踩上下文绑定。Windows 上还要在 UI 线程先建一个子 HWND，再交给 GL 当交换链表面（在渲染线程建 HWND 会和 Qt 死锁）。

`RenderThreadPool` 按这个规则 `acquire`。

---

## 5. RHI：为什么有 Vulkan 和 OpenGL 两套

GPU 厂商给的是两套完全不同的 C API。若绘制代码直接写 Vulkan，换 OpenGL 就要整份重写。

Tamias 自研了一层 **RHI（Rendering Hardware Interface）**：绘制代码只认这些动词——

- 建缓冲 / 纹理 / 着色器 / 管线
- 开始一帧、录命令、`draw_indexed`、结束并呈现

接口在 [device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)。现在两套实现，第三套是方案：

```
src/engine/render/rhi/
  device.h              抽象
  vulkan/               主后端
  opengl/               副后端（兼容性）
  wgpu/                 计划中的第三后端（见 [wgpu 接入](WGPU.md)）
```

启动时 `main()` 调用 `register_linked_rhi_backends()`，把编进来的后端登记进工厂。`RHIDevice::create()` 按设置里的 `GraphicsBackend` 选一个。

绘制代码（`draw_channel`）**没有** `#ifdef VULKAN`。差别被藏在：

- `clip_space_correction_matrix()`：Vulkan 的 NDC 是 Y 向下、Z 从 0 到 1；数学仍按 OpenGL 习惯算，最后乘这个校正矩阵。
- Shader 各编一份：`*.spv`（Vulkan）和 `*.gl.spv`（OpenGL）。

可以把它想成：导演只说「画这个网格」，翻译官分别说俄语（Vulkan）和英语（OpenGL）。OpenGL 怎么绑窗口、VBO、贴图、发 draw，见 [OpenGL 后端](OPENGL.md)。wgpu 作为第三种方言怎么接、为什么不替换这层抽象，见 [wgpu 接入](WGPU.md)（方案已定，代码未落地）。

---

## 6. Shader：给每个像素涂色的小程序

CPU 决定「画哪些三角」；**每个顶点、每个像素的颜色**由 GPU 上的 shader 算。

Tamias 的 shader 用 **HLSL** 写在 `shaders/`，构建时用 Vulkan SDK 的 **DXC** 编成 SPIR-V：

| 文件 | 干什么 |
|---|---|
| `mesh.vert.hlsl` / `mesh.frag.hlsl` | 模型：变换 + 光照 + 贴图 + 选中 |
| `sky.vert.hlsl` / `sky.frag.hlsl` | 天空上下渐变 |
| `grid.vert.hlsl` / `grid.frag.hlsl` | 地面无限网格线 |

每套编出两份：`mesh.vert.spv` 和 `mesh.vert.gl.spv`（见 [TamiasShaders.cmake](https://github.com/terry-chao/tamias/blob/main/cmake/TamiasShaders.cmake)）。运行时按后端加载。

**管线（Pipeline）** = 固定搭配：用哪两个 shader、画三角还是线段、测不测深度、是不是线框。Tamias 预创建：

| 管线 | 用途 |
|---|---|
| `sky_pipeline_` | 全屏三角，不测深度（当背景） |
| `grid_pipeline_` | 测深度、不写深度（网格不挡住埋在地下的东西） |
| `shaded_pipeline_` | 实心三角 |
| `wire_pipeline_` | 同一套 mesh shader，只把光栅变成线框 |
| `line_pipeline_` | 线段，不测深度（轴和预览线永远在最前） |

**Push constants**：每画一次物体塞给 shader 的一小包数——MVP 矩阵、模型矩阵、颜色、粗糙度/金属度、有没有 albedo/法线贴图、光线方向、是否选中、眼睛位置、显示模式、IBL mip。没有大块材质 UBO。

---

## 7. 一帧到底画了什么（顺序就是图层）

`draw_channel()` 是整套渲染的心脏。顺序固定，谁先画谁在下面（深度测试会再挡一层）：

```
1. 清屏
2. 天空      全屏大三角，上蓝下亮，不写深度
3. 地面网格  一块跟着相机 XZ 平移的大四边形，线是 shader 算的，不是真建了几千条线
4. 模型      清单里每一项一次 draw_indexed（没有合批；屏外叶子已在展平时丢掉，见 [视锥剔除](FRUSTUM-CULLING.md)）
5. 世界坐标轴  X 红 Y 绿 Z 蓝，不测深度
6. 预览线    拖墙时起点→光标
7. 把这张图画到窗口（swap / present）
```

模型那一圈对每个 `SceneDrawItem`：

1. `mesh_asset_id` → GPU 网格；没有就跳过（还没 upload）。
2. 有 albedo / 法线贴图且已上传就绑定真纹理，否则分别绑 1×1 白图和 1×1 平坦法线（Vulkan 描述符不能空绑）。
3. 填 push constants：`mvp = clip × proj × view × 物体世界矩阵`。
4. `draw_indexed`。

**现在没有：** 按材质合批、GPU instancing、透明排序。屏外叶子一期已经不进清单；一万个仍在画面里的构件仍是一万次 draw。大模型会先卡在合批，不卡在「三角太多」本身。

---

## 8. 三种显示模式

`RenderMode` 经 push constant 的 `mode` 进 fragment shader（[mesh.frag.hlsl](https://github.com/terry-chao/tamias/blob/main/shaders/mesh.frag.hlsl)）：

| 模式 | mode | 像素上做什么 |
|---|---|---|
| 线框 Wireframe | 0 | 深灰线，不打光；选中变橙 |
| 着色 Shaded | 1 | Lambert：构件识别色 × 光线点积，不读材质贴图 |
| 真实 Realistic | 2 | PBR metallic-roughness：GGX 高光 + 方向光 + split-sum IBL（工作室环境立方体） |

另外 `mode == 3` 给坐标轴/预览线：顶点自带颜色，不打光。

着色和真实都会做一件 CAD 视口常做的事：若三角的几何法线背对相机就 `discard`。这样不用依赖 GPU 的正面判定（Vulkan 翻了 Y 之后绕序很脆），模型内部不容易透出来。

真实模式里，albedo 和法线贴图默认用 **triplanar**：把世界坐标投到 XY / YZ / ZX 三个平面采样，按几何法线混合。墙、盒子、导入的 BRep 都可以贴，哪怕没有展开 UV。导入网格若带 UV（`has_texcoord`），改走网格 UV。

选中：`selected` 把颜色朝橙色 lerp，不是轮廓描边。

---

## 9. 材质从文档走到像素

文档里有材质库（`Material`）和纹理字节（`TextureAsset`，RGBA8）。引擎不依赖 Qt：解码 PNG 在 app 里用 `QImage` 做完，再 `Document::add_texture`。

路径：

```
实体.material_id
  → Material（颜色、roughness、metallic、opacity、albedo_texture_id、normal_texture_id）
    → render_items() 写进 SceneDrawItem
      → 视口 resync_textures() → upload_texture
        → draw 时 set_texture(slot 0 albedo / slot 1 normal) + push constants
          → fragment shader：先扰动法线，再 PBR 出最终 RGB
```

`Material` 是 glTF 式 metallic-roughness：**albedo** 是底色，**roughness / metallic** 是标量（没有独立粗糙度贴图），**法线贴图**只改像素法线，不改网格。没有 albedo 时 `has_albedo=0`，用纯 `base_color`；没有法线贴图时绑 1×1 平坦法线，且 `has_normal=0`，shader 跳过采样。

预设材质（`Document::seed_default_materials`）全部带 PBR 参数和法线贴图。Glass 没有 albedo（靠 `base_color` + 低 roughness + `opacity` 做半透明），其余预设有 albedo。属性面板可改 roughness / metallic，也可另选 albedo / 法线图（法线图按线性 UNORM 入库，`srgb=false`）。

### 9.1 法线贴图怎么实现

法线贴图不是另一套光照，而是 PBR 的输入：它在像素上把几何法线 `n` 扰动成更「凹凸」的 `n'`，然后 GGX / IBL 都用 `n'`。轮廓仍是原网格，侧面看不出真几何。

**生成（CPU）。** 预设贴图不是外挂 PNG。[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) 的 `make_normal_texture` 从一张高度函数做有限差分：

```
n = normalize( ((hL-hR)*strength, (hD-hU)*strength, 1) )
RGB = n * 0.5 + 0.5
```

512×512，线性空间（`TextureAsset.srgb = false`）。Concrete / Wood / Steel 用和 albedo 同源的 FBM / 木纹 / 拉丝高度；Default / Plaster 是细颗粒；Glass 是低频、很弱的起伏。上传时走 `R8G8B8A8_UNORM`，**不要**当 sRGB，否则 GPU 会做 gamma 解码，法线会偏。

**绑定（绘制）。** [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp) 只在真实模式（`mode > 1.5`）解析贴图：

- slot 0 = albedo，没有则 1×1 白
- slot 1 = 法线，没有则 1×1 `(128,128,255)`（切线空间 +Z，即「完全平坦」）
- `pc.material.z = has_albedo`，`pc.material.w = has_normal`

着色模式故意忽略材质贴图，只用构件识别色。

**采样（shader）。** [mesh.frag.hlsl](https://github.com/terry-chao/tamias/blob/main/shaders/mesh.frag.hlsl) 在 `has_normal` 时：

1. `unpack_normal`：`rgb * 2 - 1` 还原切线空间法线。
2. 无 UV：`sample_triplanar_normal`。三个轴向各采一张切线法线，按 whiteout blend 投到世界轴，再与几何法线按 `|n|` 的 4 次幂混合。这样盒子侧面、顶面都能看到凹凸，且接缝处不会硬切。
3. 有 UV：`sample_uv_normal`。用屏幕空间偏导 `ddx/ddy(world_pos, uv)` 当场建 TBN，把切线法线转到世界空间。导入 GLB 走这条。
4. 扰动后若 `n'` 背对视线，翻成朝向相机，避免凹凸把正面像素 discard 掉。

然后才采样 albedo（triplanar 或 UV），把 `(n', albedo, roughness, metallic)` 交给 `shaded_realistic`。

**和 PBR 的关系。** PBR 回答「这块表面怎么反射光」（金属度、粗糙度、环境倒影）；法线贴图回答「这个像素朝哪」。二者不是替代关系：

```
几何法线 n  +  法线贴图  →  n'
n' + albedo + roughness + metallic + 方向光 + IBL  →  最终颜色
```

只有 PBR、没有法线：材质对，但表面太平。只有法线、没有 PBR：有凹凸，但高光/金属/环境都不对。真实模式两条同时开。

IBL 是 split-sum：CPU 烘焙工作室环境立方体 → irradiance / GGX prefilter / BRDF LUT，fragment 用 `n'` 采漫反射、用 `reflect(-v, n')` 采高光。没有阴影、没有自定义 HDRI。

---

## 10. 和「改参数」怎么接上

参数化不发生在渲染里。闭环是：

```
属性面板改 depth
  → SetFeatureParamCommand 改特征树
    → OCCT 重算 BRep → 新 MeshCpu 写进 MeshAsset
      → 视口 upload_mesh（同一 asset_id，换 GPU 缓冲）
        → 下一帧清单仍指向这个 id，画面就是新形状
```

渲染只负责「这个 id 现在对应哪份三角」。配方和 BRep 见 [特征树求值器](FEATURE-TREE-EVALUATOR.md)。

---

## 11. 现在刻意没做的（避免误会）

这些在 [路线图](ROADMAP.md) 里，**不是漏画**，是还没做：

- 合批 / instancing（大 BIM 的下一道性能命门；视锥一期已落地，二期/三期见 [视锥剔除](FRUSTUM-CULLING.md)）
- 截面剖切、Hidden Line
- 阴影、AO、自定义 HDRI（工作室 split-sum IBL 已有）
- 渲染侧场景图（VSG 式节点 + 命令图）——现在每帧展平
- 多选轮廓、按类型分类着色
- 粗糙度 / 金属度贴图（现在是每材质一个标量）

拾取 BVH 已经有了，但只给鼠标点选用，**没有**拿来做绘制剔除（三期才复用）。一期剔除走的是每个叶子的 `world_bounds`。

---

## 12. 一张总图

```
特征树 / STEP / OBJ
        │ tessellate 或直接加载
        ▼
   MeshCpu（CPU 三角）          Scene（谁在哪）
        │ upload                     │ render_items() 展平
        ▼                            ▼
   GpuMesh 缓存              SceneDrawItem 列表
        │                            │
        └──────── 视口打包 FrameSubmission ────────┐
                                                 ▼
                              RenderThread.draw_channel
                                                 │
                    天空 → 网格 → 每个 item 一次 draw → 轴
                                                 │
                              RHI：Vulkan 或 OpenGL（wgpu 见方案）
                                                 ▼
                                              窗口像素
```

---

## 附录：源码地图

| 文件 | 角色 |
|---|---|
| [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp) | 相机、提交帧、上传网格、点选 |
| [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) | `render_items()` 展平清单；`seed_default_materials()` 预设 albedo / 法线 / PBR |
| [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h) | `SceneDrawItem` / `RenderMode` |
| [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) | 线程、上传、一帧绘制顺序 |
| [rhi/device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h) | GPU 抽象 |
| [rhi/vulkan](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/vulkan/vulkan_device.cpp) / [rhi/opengl](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp) | 两种实现；OpenGL 细节见 [OpenGL 后端](OPENGL.md)；第三后端方案见 [wgpu 接入](WGPU.md) |
| [material.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/material.h) | `Material` / `TextureAsset` |
| [mesh.frag.hlsl](https://github.com/terry-chao/tamias/blob/main/shaders/mesh.frag.hlsl) | 线框 / 着色 / 真实（PBR + 法线采样） |
| [rhi_backends.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/rhi_backends.cpp) | 启动时登记后端 |
