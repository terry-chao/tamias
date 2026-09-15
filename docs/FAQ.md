# 答疑（Q&A）

> 这里集中回答散落在各处的「为什么」：场景规模、BRep 与 OCCT、mipmap、场景管理（UE / OSG / VSG）、RHI、插件 C#、拾取与容差、相机旋转中心、绘制流程、计时工具、文字、浮点精度、LOD、以及明确不做的 sketch。
>
> 每条结论都指到当前代码或对应模块文档。想先读体系，走 [总览](index.md) → [架构](ARCHITECTURE.md) → [渲染管线](RENDERING.md)；本文是它们的「问题索引」，不重复展开细节。

---

## 0. 问题索引

| # | 问题 | 落到哪 |
|---|---|---|
| 1 | 十几亿顶点的汽车场景怎么管理、少卡顿 | §1 · [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) |
| 2 | OCCT 的 modeling data / 文档 / 算法，BRep 里存什么 | §2 · [MODELING-KERNEL.md](MODELING-KERNEL.md) |
| 3 | 圆柱 ∩ 棱锥，BRep 里存什么 | §3 |
| 4 | 为什么 mipmap 生成得快 | §4 |
| 5 | UE 的场景管理 | §5 |
| 6 | OSG 梳理一遍、StateSet | §6 |
| 7 | VSG 和 OSG 差在哪 | §7 |
| 8 | 透明的、半透明的画在什么顺序 | §8 |
| 9 | 临时的、实例化的图元怎么处理 | §9 |
| 10 | 场景和文档的处理关系 | §10 |
| 11 | 为什么能避免上下文切换 | §11 |
| 12 | RHI 是基于什么设计的 | §12 |
| 13 | 插件里的 C# 事件怎么通信 | §13 |
| 14 | pick 射线的容差怎么算 | §14 |
| 15 | pick 怎么找到最前面的实体 id | §15 |
| 16 | 旋转中心是鼠标 / 场景 / 选中实体 | §16 |
| 17 | 绘制流程：事件还是状态机 | §17 |
| 18 | 计时工具怎么拿到函数调用栈 | §18 |
| 19 | 文字渲染、闪烁、显示顺序 | §19 |
| 20 | 大场景 float 影响渲染 | §20 |
| 21 | LOD 是每个模型拿还是池子，数据源在哪 | §21 |
| 22 | sketch 模块 | §22 |

---

## 1. 十几亿顶点的场景怎么不卡

### 结论

**不能优化「画出十几亿三角」，只能让这十几亿永远不以全分辨率驻留显存、也不以「一物体一次 draw」提交。**

一帧真正贡献颜色的三角数量级是「像素的几倍」，不是「模型里有多少」：

| 分辨率 | 像素 | 目标提交三角 | 目标 draw |
|---|---|---|---|
| 1080p | ~2.1e6 | 8e6–2e7 | < 2000 |
| 4K | ~8.3e6 | 2e7–4e7 | < 2000 |

算一笔账就明白为什么必须虚拟化：当前 `Vertex` 是 `position(12) + normal(12) + uv(8) + color(12) = 44` 字节（[mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h)），索引 `uint32`。10 亿三角即使按「0.5 顶点 / 三角」焊接，光是顶点就是 22 GB，加索引 12 GB —— **装不下，CPU 再留一份更不用谈**。

问题必须拆成三句，顺序不能反（见 [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md)）：

1. **少生成**：远处、隐藏楼层、关掉的专业，不要用 0.05 的 deflection 离散。
2. **少驻留**：显存是 LRU 预算，不是「文档里有的网格全 upload」。
3. **少提交**：同几何用 instancing；屏外 / 小于 1 像素 / 被楼挡住的，不发 draw。

### 汽车场景比 BIM 更麻烦的地方

BIM 的「三角多」常常是「同一段墙截面复制八千次」，所以实例化收益巨大。**汽车装配体两头都占**：

| 对象 | 特征 | 该走哪条路 |
|---|---|---|
| 标准件（螺栓、卡扣、支架、线束夹） | 同一份几何复用几千次 | L0 指纹 intern + L1 instancing（[INSTANCING.md](INSTANCING.md)） |
| 车身覆盖件、A 面曲面 | 每块都不同，且是 BRep | 按屏幕误差重离散（G3），不要上 Nanite |
| 导入的扫描网格 / 无参数网格 | 没有「再离散一次」这条退路 | G5 meshlet + 流式（尚未做） |
| 总装整包 | 几十万构件、扁平层级 | 空间 BVH 剪枝（G2 三期）+ 像素剔除 |

一句话差别：**有 BRep 的 CAD 比游戏更适合「按屏幕误差重离散」**，所以顺序必须反过来 —— 先复用几何、先别画看不见的、先换粗网，最后才对无 BRep 的巨型三角汤做 cluster DAG。

### 卡顿（hitching）从哪来

「卡顿」和「帧率低」是两件事。帧率低是每帧都慢；卡顿是**某几帧突然慢**。本仓库里三个已知来源：

| 来源 | 现状 | 对策 |
|---|---|---|
| OCCT 离散占主线程 | OCCT 不在 UI / 渲染线程跑，走 `TessWorker` 后台队列（[tess_worker.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/tess_worker.h)） | 已落地。Emscripten 没 pthread 时靠 `pump(max_jobs)` 把一帧的工作摊到多帧 |
| 打开大 STEP 首帧出不来 | 导入时只算 `Shape::bounds()`，按 `LodRequest` 懒离散（[occt_shape_ops.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt/occt_shape_ops.cpp)） | 已落地：先出 L0 盒 / 粗档，细档异步补 |
| upload 阻塞 UI | `upload_mesh` / `upload_texture` 会阻塞 UI 直到 GPU 侧写完（[RENDERING.md](RENDERING.md) §4） | 部分落地：`request_upload_mesh` 提交即返回；大贴图的 mip 链仍在渲染线程算（见 §4） |

还有一类不是崩溃而是**手感**问题：LOD 档次抖动。修法是滞回，不是加预算 —— `select_mesh_lod` 升档要 1.25×、降档要 0.8×（[mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/mesh_lod.h)）。

### 现在会在哪爆

| 环节 | 现状 | 十几亿时还缺 |
|---|---|---|
| 语义树 | `Scene::find` 线性扫；`recompute_world()` 全量 | 十万节点时，改一根柱也扫全树 |
| 展平 | `render_items()` 全量清单（不管视锥也照出，好让留存树完整） | 清单本身可以上百万条 |
| 录制 | 视锥 + 隐藏 + 屏幕误差选档；合批键 `gpu_mesh_id` | 远近不同档自然拆两批；GPU-driven 没做 |
| 驻留 | `ResidentCache` 默认 2 GB LRU，淘汰 GPU buffer | 非编辑对象的 CPU `MeshCpu` 仍留在 Document |
| 拾取 | 物体级 BVH；叶子打 `resolved_mesh`（Work / Close，不打 L0 盒） | 单个 5000 万三角零件点选仍会卡；拾取树不参与绘制剔除 |
| RHI | `instance_count` + instance 缓冲已接 | indirect / GPU-driven 没做 |

可执行清单（按 ROI 排）：**先 intern + instance，再层级剔除 + 像素剔除，再自适应离散 + 显存 LRU，然后面级流式，最后才是 meshlet 与 GPU-driven。** 不要在 G1–G3 没做完时开 Nanite。

---

## 2. BRep 里到底存了什么，OCCT 怎么存

### 先分清三份数据（这是最容易混的地方）

| 数据 | 存什么 | 在哪 | 是真相源吗 |
|---|---|---|---|
| **文档**（`.tdoc`） | 特征树 + 参数 + 材质 / 贴图 / BIM 语义 + 网格资产表 | `Document`，序列化见 [document_io.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.h) | **是。** 唯一可编辑的主数据 |
| **BRep** | 精确几何与拓扑（曲面方程、面边界、容差） | 内核里（OCCT 的 `TopoDS_Shape`），`Body` 只给不透明句柄 | 不是。是**求值结果**，可重建 |
| **三角网** | `MeshCpu` / `GpuMesh` | `MeshAsset` + 显存 | 不是。是**多分辨率缓存** |

`Feature` 的定义很直白：`FeatureKind` + `inputs`（依赖的特征 id）+ `params`（命名参数表），旁边就是求值器要跑的东西（[feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h)）。

`.tdoc` 是「magic + chunk」的分块格式，一共 11 个 chunk：`META` / `MESH` / `SCEN` / `FEAT` / `MATL` / `TEXT` / `RELA` / `STRY` / `GRID` / `DRWG` / `VIEW`。所以要精确说：

- **没有一个 `TopoDS_Shape`。** BRep 从不落盘，永远靠特征树重算 —— 换内核、升级 OCCT、修离散算法，都不该让老文档失效；
- **有网格**（`MESH` chunk，`MeshCpu` 的顶点 + 索引）。原因很实在：导入的 OBJ / GLB / STEP 离散结果**没法从参数重建**，不存就丢了；
- 对**自己建的实体**，主数据仍是特征树，网格是「可以重算的缓存」—— 改参数时重算并覆盖同一 `mesh_asset_id`。

这也是 [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) 把「**不把最高精度网写进 `.tdoc` 当主数据**」列为后续工作的原因：大 tessellation 缓存应该独立成 LevelDB 之类的缓存层，而不是塞进整文件 `binary_archive`。

### `TopoDS_Shape` 是什么形状

OCCT 的 BRep 是**拓扑 + 几何**两层，靠引用拼起来：

```
TopoDS_Shape = TShape（共享的拓扑数据）+ TopLoc_Location（放在哪）+ TopAbs_Orientation（朝哪）
  拓扑（只管「谁挨着谁」，不含坐标）
    Vertex → Edge → Wire → Face → Shell → Solid → CompSolid → Compound
  几何（形状本身，由 BRep_TFace / BRep_TEdge 里的 Handle 指向）
    Face → Geom_Surface（Geom_Plane / Geom_CylindricalSurface / Geom_BSplineSurface …）
           + 一圈闭合 Wire 定义它在参数域里的裁剪边界
    Edge → 一条 3D 曲线（Geom_Line / Geom_Circle / Geom_BSplineCurve …）
           + 每个相邻面各一条 2D pcurve（面参数域里的 UV 曲线）
           + 容差 tolerance
```

几个必须记住的点：

- **几何是共享的。** 圆柱侧面是一个 `Geom_CylindricalSurface` 对象，不是「一堆三角」。一个面只是「这个曲面 + 一圈边界线」。
- **pcurve 是 BRep 的关键。** 一条 3D 边在参数域里长什么样，决定了「这个面到底被裁到哪里」，也是判定点是否在面内的依据。没有 pcurve，拓扑信息就丢了。
- **朝向 `TopAbs_REVERSED` 表示法线朝内。** 离散时要把三角形绕序反过来（[occt_shape_ops.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt/occt_shape_ops.cpp) 里就是这么处理的）。
- **容差是拓扑的一部分。** 布尔 / 倒角的容差会写回边和顶点，直接决定后续能不能缝合。
- **三角化是缓存在 Face 上的**（`BRep_TFace` 里的 `Poly_Triangulation`），不改变 BRep 本身。Tamias 的 `tessellate` 就是跑 `BRepMesh_IncrementalMesh`，再把这份三角抠出来。

### Tamias 这一侧的边界

内核接口刻意停在「BRep 操作」层（[kernel.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/kernel/kernel.h)）：

- `Body` 是 `shared_ptr<const Body>` 不透明句柄，**外面永远看不到 `TopoDS_Shape`**；
- `EdgeId` 只在**一个 Body 的生命周期内**有效；跨求值、跨会话的持久标识由中间层的**几何指纹**负责（[edge_fingerprint.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/edge_fingerprint.cpp)），不是内核的事；
- 唯一允许 `#include <BRep*.hxx>` 的地方是 `src/engine/modeling/occt/`。

离散时按 Face 走，并把每个面在索引里的范围记进 `MeshCpu.faces`（`MeshFaceRange` + 面 AABB，见 [mesh_face_range.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh_face_range.h)）。这样以后「按面剔除 / 按面剖切 / 按面流式」不需要改顶点格式，也不动 `.tdoc` 序列化。

为什么不用 OCCT 自己的历史（`BRepTools_History` / `Modified` / `Generated`）？因为它只在**同一个进程、同一次布尔**里有效，一存盘就没了。Tamias 要的是「改个参数、倒个角，那条边还认得出是它」，所以用几何指纹（索引 + 位置 / 方向 / 长度 / 相邻面法线）而不是内核句柄。

---

## 3. 圆柱 ∩ 棱锥，BRep 里存什么

假设圆柱（侧面是 `Geom_CylindricalSurface`，上下底是 `Geom_Plane`）和棱锥（`n` 个侧面平面 + 一个底面平面）做布尔。OCCT 走 `BRepAlgoAPI_Cut / Common / Fuse`（Tamias 的 `OcctKernel::boolean`，[occt_kernel.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt/occt_kernel.cpp)）。结果**不是**「记录了一次运算」，而是**一整套新的精确 BRep**。

### 结果里逐个数

| 类别 | 存什么 | 圆柱 ∩ 棱锥具体是什么 |
|---|---|---|
| **曲面** | 每种曲面一个解析对象 | 圆柱侧面仍是 `Geom_CylindricalSurface`；棱锥侧面仍是 `Geom_Plane`。**曲面没有被三角化，也没有被近似** |
| **面 `TopoDS_Face`** | 曲面 + 一圈 `Wire` | 原来的面被**裁剪**成新面：面身份（曲面）不变，**边界线换成了包含交线的新 Wire** |
| **边 `TopoDS_Edge`** | 3D 曲线 + pcurve + 容差 | ① 圆柱侧面 ∩ 棱锥侧面 = 一段空间曲线（解析可解时是 `Geom_Ellipse` 的一段，不可解时退化成 `Geom_BSplineCurve`）；② 圆柱上 / 下圆被交点**打断成若干段弧**；③ 棱锥的棱边被交点打断成段 |
| **顶点 `TopoDS_Vertex`** | 一个点 + 容差 | 交线与圆 / 棱边的**交点** —— 这些顶点原来两个体里都不存在，是布尔算出来的 |
| **pcurve** | 每条边在每个相邻面参数域里的 2D 曲线 | 交线在圆柱参数域里是「角度 / 高度」上的一条曲线，在棱锥侧面参数域里是另一条。两边都要有，否则面无法判定自己的裁剪范围 |
| **朝向** | `TopAbs_FORWARD / REVERSED` | Cut 之后某些面会翻向（法线朝内 → 朝外），离散时要按 `reversed` 反过来 |
| **容差** | 每条边 / 每个顶点一个实数 | 布尔缝合时算出来的；它决定后续还能不能再倒角 |
| **三角化缓存** | `Poly_Triangulation`（可选） | 布尔刚做完时通常**没有**；Tamias 调 `BRepMesh_IncrementalMesh` 才生成 |

### 为什么必须保留精确曲面

如果布尔只输出三角，会出现三件坏事：

1. **改参数没法重算** —— 圆柱半径从 20 改到 25，三角汤里那个「圆」已经不存在了；
2. **倒角 / 抽壳没法做** —— 那些算法要的是解析曲面和真实交线，不是近似折线；
3. **精度不可控** —— 在 0.05 的 deflection 上做布尔，交线位置的误差是离散误差的量级，不是内核的量级。

所以正确顺序永远是：**精确布尔 → 精确 BRep（带交线）→ 按当前相机误差离散成三角**。三角只是最后一步、可丢弃的缓存。

### 和 Tamias 的接口对上

`ModelKernel::boolean(a, b, op)` 返回一个新的 `BodyRef`，`BooleanOp{Fuse, Common, Cut}` 作为 `Boolean` 特征的 `operation` 参数存进 `.tdoc`（[feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h)）。**第二个后端 Truck 也实现了布尔**（经 `truck-shapeops`），所以同一份 `.tdoc` 可以被不同内核求值 —— 这正是分层要换来的东西。跨后端验收见 `tests/kernel_conformance_tests.cpp`（[TESTING.md](TESTING.md)）。

已知坑：**共面的两个体做布尔是退化情形**，Truck 直接返回 none，验收用例里把工具体挪开一点避开（[MODELING-KERNEL.md](MODELING-KERNEL.md) §6）。

---

## 4. 为什么 mipmap 生成得快

### 算术上的快：每一级只有上一级的 1/4

`build_texture_mips`（[texture_mips.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/texture_mips.cpp)）是逐级 2×2 box filter：第 `i` 级从第 `i-1` 级降采样，尺寸 `w/2 × h/2`。总工作量：

```
N + N/4 + N/16 + … = (4/3) · N
```

也就是说，**整条 mip 链的代价只有 1.33 张原图**，而不是「每级都重新从原图采样」的 `levels × N`。后者在 128×128 上就要差十几倍。

### 访存上的快：顺序读、顺序写

`downsample` 是双重循环，源按行读、目标按行写，两个缓冲都是顺序访问，没有随机跳转，循环内只有一次分支判断（sRGB 贴图多两次 `pow`）。这种循环对 cache 和编译器自动向量化都友好。

### 一次生成，不是每帧生成

mip 在做 `upload_texture` 时**生成一次**（[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) 里 `mips = build_texture_mips(asset)`，然后逐级 `write_subresource`），之后每帧只是采样。所以它不进每帧预算。

### 渲染为什么也快（这才是 mipmap 的本职）

| 机制 | 效果 |
|---|---|
| 缩小走低 mip | 每个屏幕像素只碰 1 个 texel（双线性 4 个），而不是从几十个 texel 里随机抓一个 |
| 纹理 cache 命中率 | 邻近像素采的是同一块低分辨率区域，cache 不抖 |
| 消除摩尔纹 / 闪烁 | 远处纹理不再「每帧随机采样」，运动时不再闪 |
| 采样量随屏幕，不随纹理 | 4K 贴图缩到 100×100 显示，代价和 100×100 贴图几乎一样 |

反过来说：**mipmap 不是「提高质量换性能」，而是「本来就该这么过滤」** —— 不加 mip 的缩略采样在数学上就是欠采样。

### 遗留的坑

- 生成发生在渲染线程的 `upload_texture` 里，而这条路径**会阻塞 UI** 直到写完（[RENDERING.md](RENDERING.md) §4）。8K 材质一次性建链会卡一下。
- 没有走 GPU 的 blit 生成（`vkCmdBlitImage` / `glGenerateMipmap`），全在 CPU。想要更快 / 更省 CPU，这两条路都比手写 box filter 强；代价是 sRGB 行为和精度没那么直白。
- 没做各向异性过滤（`GL_TEXTURE_MAX_ANISOTROPY` / `VkSampler` 的 anisotropy），斜面纹理在掠射角下会糊。

---

## 5. UE 的场景管理（外部对照）

> 这一节讲的是 **Unreal Engine 怎么做**，不是本仓库的实现。列出来是为了对照「同一个问题，成熟工业方案怎么拆」，最后一张表把概念映射回 Tamias。

### 分层

```
UWorld                       一局游戏 / 一个关卡的总容器
  └─ ULevel                  持久层 + 若干子层；是流式加载的单位
       └─ AActor             「一个东西」（可放置、可复制、可存档）
            └─ UActorComponent / USceneComponent   能力与变换层级
                 └─ UPrimitiveComponent（StaticMesh / SkeletalMesh / Instanced）
```

### 四个机制，各解决一件事

| 机制 | 解决什么 | 怎么做的 |
|---|---|---|
| **World Partition + Streaming** | 世界太大，一次装不下 | 按空间切成 cell，跟着玩家位置流式加载 / 卸载；每个 cell 是一个 `ULevel` |
| **HLOD** | 远处整片建筑太细 | 离线把远距离的一组静态网格**合并**成一个低模代理，靠近时再切回单体 |
| **Auto-Instancing / ISM / HISM** | 同网格重复几千次 | 相同 mesh + 相同材质的绘制合并成一次 instanced draw；HISM 带层级和每实例 LOD |
| **Nanite** | 单个网格三角太多 | 虚拟几何：离线把网切成 cluster 建 DAG，按屏幕误差流式提交，配合 HZB 遮挡剔除和软件光栅器 |

再加上 **PrimitiveSceneProxy**（渲染线程用的代理结构）、**预计算可见性 / Distance Culling / Screen Size LOD**、每帧的 occlusion query。核心思想和 [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) 完全一致：**把「模型里有几个三角」和「这一帧提交几个三角」彻底拆开。**

### 映射回 Tamias

| UE | Tamias | 现状 |
|---|---|---|
| `UWorld` / `ULevel` | `Document` | 已落地 |
| `AActor` | `Entity` + `SceneNode` | 已落地（BIM 语义在 [BIM.md](BIM.md)） |
| `UPrimitiveComponent` | 有 `mesh_asset_id` 的 `SceneNode` | 已落地 |
| 渲染线程代理（SceneProxy） | 留存 `RenderNode` 树 + 每帧 `SceneDrawItem` 清单 | 已落地（[SCENE-GRAPH.md](SCENE-GRAPH.md)） |
| ISM / HISM | `GpuInstance` + `BatchKey` 分桶 | 已落地，半透明不合批 |
| 每实例 LOD | `MeshLod` + `select_mesh_lod` 滞回 | 已落地 |
| Streaming cell | 无（楼层 / 类别隐藏集是前身） | 未做 |
| HLOD | 无（BRep 在，可按误差重离散，不必先合并） | 不上 |
| Nanite | 无 | **刻意不做**：有 BRep 时重离散更便宜 |
| HZB 遮挡剔除 | 无 | 排在合批之后 |

**最重要的差别：UE 的网格是「导入即终态」，Tamias 的网格是缓存。** 所以 UE 需要在三角上继续分层的 Nanite，而 Tamias 可以直接问：你离这么远，我为什么要离散这么细？

---

## 6. OSG 梳理一遍

> OpenSceneGraph。它是**留存式场景图 + 隐式状态机**的经典实现。理解它的 `StateSet` 和渲染 bin，是理解「为什么场景图能减少状态切换」的最短路径。

### 节点家族

```
osg::Node
  ├─ osg::Group            子节点列表（组织 / 层级）
  ├─ osg::Transform        变换节点（子树的局部矩阵）
  ├─ osg::LOD / PagedLOD   按距离 / 屏幕大小切孩子；PagedLOD 配 DatabasePager 后台加载
  ├─ osg::Switch           开关（可见性）
  ├─ osg::OccluderNode     遮挡体
  └─ osg::Geode            叶子容器
       └─ osg::Drawable    真正会画的东西（Geometry / ShapeDrawable …）
```

### 每帧两遍遍历

1. **Cull 遍历**：从相机出发做视锥剔除、LOD 选择，把「要画的东西」收集成 `RenderGraph`；节点类型是 `RenderLeaf`（一次 draw）和 `RenderBin`（一组 draw）。
2. **Draw 遍历**：沿 `RenderGraph` 走，**先按状态分组、再下发 draw**。

关键在第 2 步：draw 不是按场景图顺序画，而是**按最终状态排序后画**。

### `osg::StateSet`：状态是一份可共享的数据

`StateSet` 里装四样东西：

| 列表 | 装什么 |
|---|---|
| `AttributeList` | 材质、纹理、混合、深度测试、CullFace、线宽… |
| `ModeList` | 开关型的 GL 模式（`GL_LIGHTING`、`GL_BLEND`、`GL_DEPTH_TEST`） |
| `UniformList` | 着色器 uniform |
| RenderBin 设置 | `RenderBinMode`（是否继承父 bin）、`bin number`、`bin name` |

状态沿树**隐式继承**：子节点没设的，用父节点设的。

### 为什么能省状态切换

两件事同时发生，才叫「减少状态切换」：

1. **共享**：`StateSet` 是引用计数的。一万个 `Geode` 都 `setStateSet(shared)`，内存里只有一份；树也因此形成「状态相同的子树」。
2. **归并**：cull 阶段建的是 `StateGraph` —— **一个状态图的节点 = 一个 StateSet**。相同 `StateSet` 的 `RenderLeaf` 全部挂到同一个节点下。draw 遍历时，同一个状态节点下的 draw 连续画完再换状态。

结果：状态切换次数 ≈ **不同 StateSet 的数量**，而不是 draw 次数。一百万个 draw、三种材质，状态只切三次。

### 透明怎么排

OSG 分了「不透明 bin（按状态排序）」和「透明 bin（`DepthSortedBin`，按相机距离从后往前排）」。**顺序来自 bin 的排序策略，不来自场景图。** 所以同一个 `StateSet` 的树叶完全可以被拆到不同 bin：bin 决定顺序，StateGraph 决定状态。

### 要注意的代价

- 状态继承是**隐式**的：改父节点的 `StateSet` 会静默影响整棵子树，出了问题很难定位；
- `StateGraph` 每帧重建，大场景下 cull 本身会成为瓶颈；
- `PagedLOD` 的异步加载在后台线程，主线程偶尔要等（或出现低模 → 高模的跳变）。

---

## 7. VSG 和 OSG 差在哪

> VulkanSceneGraph。同一批人（Robert Osfield）做的下一代，**设计前提从「OpenGL 的隐式状态机」换成了「Vulkan 的显式命令缓冲」**。

| 维度 | OSG | VSG |
|---|---|---|
| 目标 API | OpenGL（兼容老硬件、隐式状态） | Vulkan（显式、多线程录制） |
| 状态 | `StateSet`，**隐式继承** | `StateCommand`，**显式下命令**，沿遍历线性累积 |
| 每帧做什么 | 记录出命令，draw 时下发 | **把命令录进 `CommandBuffer`**，之后可直接重放 |
| 多线程 | 基本单线程 cull / draw | 多 command buffer 并行录制 |
| 描述符 | 逐 draw 绑 GL 状态 | descriptor set / bindless 池化 |
| 视图组织 | Viewer → Camera → Scene | `View` + `CommandGraph` + `RenderGraph`，多视图可共享录制结果 |

**Tamias 抄的是 VSG 这一支。** [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.h) 开头写得很直白：

> 渲染场景图骨架（VSG 式：节点 + 访问者 + 命令图状态）…… 语义是 VSG StateCommands 式的显式命令图 —— 状态沿遍历线性累积，子树要覆盖什么就在自己的 StateGroup 里再下命令，**不做 OSG StateSet 式隐式继承**。

节点类型：`GroupNode` / `TransformNode` / `StateGroupNode` / `DrawableNode`；状态命令：`BindMaterialCommand` / `SetSelectedCommand` / `SetLinesCommand`；`RecordCommands` 是那个访问者：命中 `StateGroup` 就把命令「录」进上下文，命中 `Drawable` 就消费上下文状态下发 draw。

为什么不做隐式继承？因为 Tamias 的渲染树是**每帧从展平清单重建**（加脏标记增量同步）的，结构由 `Document` 决定，不是让用户拿 `StateSet` 手搭的。显式命令更好预测、更好测、以后也更容易并行录制。

---

## 8. 透明 / 半透明的绘制顺序

### 铁律

- **不透明**：顺序无关（深度测试 + 深度写），可以随意合批、instance。
- **半透明**：**必须从后往前画**。`ONE / ONE_MINUS_SRC_ALPHA` 混合不可交换 —— A 盖 B ≠ B 盖 A。
- **两者不能同批**：一旦把玻璃塞进不透明桶，深度写会把后面的东西挡掉。

### Tamias 现在怎么做

渲染线程每个 channel 走**两遍**（[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp)）：

```
第一遍  RecordCommands(ctx)              ← 跳过 opacity < 1 的
第二遍  ctx.transparent_pass = true      ← 只画 opacity < 1 的，blend pipeline
```

判定在 `RecordCommands::apply(DrawableNode&)` 里：

```cpp
const bool transmissive = use_material && ctx_.material_opacity < 0.999f;
if (ctx_.transparent_pass) { if (!transmissive) return; }
else if (transmissive) return;
```

半透明批次**不合批**：直接 `flush_batch(batch)` 单发一份（`batch.key.transparent = true`），就是为了不把不同深度的玻璃塞进同一个 instance 批次。混合管线 `depth_write = false`，深度测试仍然开着（这样玻璃后面的实体还能挡住它）。

| 类别 | 归属 | 原因 |
|---|---|---|
| 不透明实体 | 第一遍，`BatchKey` 自由 instance | 顺序无关，收益最大 |
| Alpha test（cutout） | 并入不透明 | `discard` 不破坏合批 |
| 半透明（玻璃 / 幕墙面板） | 第二遍，一物一 draw | instance 会打乱 back-to-front |
| 选中填充 | 实例 flags 里带 `selected` | 不能按 `selected` 拆桶，否则框选一千个 = 一千批 |
| 轮廓 / 夹点 / 网格 / 轴 / 坐标轴 | 独立 overlay，永不进模型桶 | 状态完全不同（`depth_test = false`） |

### 现状缺口（诚实说）

第二遍**没有做深度排序**：它按渲染树遍历顺序一路 `flush_batch`。所以：

- 场景里只有一两块玻璃时看不出问题；
- 同屏有多块互相重叠的玻璃时，顺序可能是错的（近的比远的先画）。

补法三条，代价递增：① 按节点中心到相机的距离排序后再遍历；② depth peeling（每层一遍，N 层 N 遍）；③ OIT（per-pixel linked list 或 weighted blended）。**做 ①② 之前先确认真的需要** —— 建筑场景里大多数「透明」是幕墙，排序错误通常看不出来，而 ①② 会打破现在的合批收益。

---

## 9. 临时的与实例化的图元

### 三类图元的归属

| 类别 | 例子 | 走哪条路 |
|---|---|---|
| **模型**（留存） | 墙、板、导入网格 | GPU 网格留存 + 每帧清单 + `BatchKey` 实例化 |
| **临时 / overlay**（每帧重建） | 拖墙预览线、草图曲线、贝塞尔控制多边形与控制点、夹点、捕捉点、轴网、网格线、框选、调试段 | 独立 overlay 通道，每帧重传，`depth_test = false` |
| **二维图纸** | DXF / DWF 的曲线与文字 | 完全另一条路：Qt `QPainter`（[DRAWING.md](DRAWING.md)） |

临时图元的数据都在 `FrameSubmission` 里，每帧现填（见 [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h)）：

```
preview_polyline / preview_control_polyline / preview_points
grip_points / snap_point / debug_vertex
grid_line_segments / grid_preview_segments / grid_selected_segments
debug_line_segments
```

渲染侧统一用 `preview_line_mesh_`（一段单位线段）+ 每段一个 `GpuInstance`，逐段 `set_push_constants` + `draw_indexed`（`draw_segment` / `draw_polyline` / `draw_dashed_polyline`）。**它们不进 `BatchKey` 的模型桶**：状态（无光照模式、无深度测试）跟实体完全不同，混进去只会把模型批次拆碎。

### 为什么临时图元要单开通道

1. **生命周期不同**：模型网格「几何变了才上传」，预览线「每帧都变」；
2. **深度语义不同**：预览线要「永远看得见」（`line_pipeline` 不测深度），模型要遮挡；
3. **不污染统计**：`RenderFrameStats::triangles` 只算三角面，`as_lines` 的不计。

### 实例化怎么处理

实例数据是 `GpuInstance`（80 字节：world 3×4 仿射矩阵 + 颜色 + 材质，[gpu_instance.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/gpu_instance.h)），走**顶点 instance rate** 的第二个顶点缓冲，而不是一个一个 push constant：

```
RecordCommands::enqueue(batch, instance)   → 按 BatchKey 找桶
RecordCommands::flush_batch(batch)         → 写实例缓冲 + instance_count = N
```

三个细节：

- **实例里存 world，不存 MVP**：阴影级联、剖切、多视口换的只是 view / proj，同一张实例表能复用；
- **N = 1 也走同一条路径**，禁止「单物体用旧 push constant、多物体用新 instance」两套实现；
- **`transparent = true` 的批次立即 flush**，不参与合并（理由见 §8）。

---

## 10. 场景和文档的关系

### 三层，各管一件事

```
Document（主数据）
  ├─ entities_    实体表（FeatureModel + 材质 + BIM 语义）
  ├─ meshes_      MeshAsset（CPU 网格，按指纹 intern 共享）
  ├─ tess_cache_  geometry_id → {coarse, work, close}    ← LOD 的唯一真相源
  └─ scene_       SceneNode 树（parent / local_transform / world_bounds / selected）
        │  render_items(frustum)
        ▼
  SceneDrawItem 清单（每帧的「货单」：mesh_asset_id + world matrix + 材质 + selected）
        │  FrameSubmission（相机 + 清单 + overlay + lod_sets 快照）
        ▼
  RenderThread 的留存 RenderNode 树 → RecordCommands → GPU
```

| 谁 | 是什么 | 谁拥有 |
|---|---|---|
| **Scene（语义树）** | 父子 + 变换继承 + 包围盒 + 选中 | `Document`。是**层级唯一真相源** |
| **展平清单** | 烘掉层级的「网格 + 世界矩阵」列表 | 每帧重新生成 |
| **渲染树** | `RenderNode` 投影，只保留 draw 需要的字段 | 渲染线程，半留存 |

一致性靠两条：

- **`Scene::recompute_world()`** 自顶向下算 `world = parent_world × local`，自底向上合并包围盒，并做防环；
- **`scene_generation` + `scene_dirty_ids`**：代次没变就不重建树；代次变了且有脏 id 就走 `update_scene_graph` 增量更新（就地改世界矩阵 / 状态 / Drawable，或在 `node_index` 里定位后删除子树）；脏 id 为空（clear / 整档恢复）时退回全量重建。

### 「半留存」留什么

| 留存 | 不留存 |
|---|---|
| GPU 网格（`upload_mesh` 幂等，几何变了才重传） | 场景结构（每帧重新交一份清单） |
| GPU 纹理（同上） | 展平结果 |
| 渲染侧 `RenderNode` 树（跨帧复用，脏标记更新） | —— |

这和 OSG 那种「三角形算好就常驻、重绘只遍历结构」不同，也不同于「每帧重建一切」。取中间：**积木留在显存，站位每帧重报。**

### 数据往哪个方向流

只有一条方向：**编辑 → 命令 → Document → 脏标记 → 视口重提交 → 渲染线程**。

点选不走这条路进 GPU：视口在 CPU 上打 BVH，命中后改 `SceneNode.selected`，下一帧清单里 `selected = true`，shader 把颜色偏橙。**渲染侧永远不知道「这是一面墙」。**

---

## 11. 为什么能减少上下文切换

「上下文切换」在这套代码里有两层含义，两层都在被处理。

### 第一层：GPU 状态切换（驱动侧）

每次换 pipeline、换贴图绑定、换顶点缓冲，驱动都要做一次状态校验 / 重编程。切换代价**比 draw 本身还贵**，而且贵得多。所以减少状态切换不是「省几条指令」，是省掉绝大部分驱动工作。

本仓库的手段就是 **`BatchKey` 分桶**（[batch_key.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/batch_key.h)）：

```cpp
struct BatchKey {
  std::uint64_t gpu_mesh_id;       // 顶点缓冲必须相同才能 instance
  PipelineState* pipeline;         // 切 PSO 比 draw 贵
  Texture* albedo, *normal, *orm;  // 描述符切换
  bool lines;                      // 线 / 三角不要混
  bool transparent;                // 半透明必须独立序列
};
```

`RecordCommands` 先按这个键把 draw 收进桶（`enqueue`），遍历结束后统一 `flush_all`。于是：

| 不排序 | 分桶后 |
|---|---|
| 每画一个物件就 `set_pipeline` + 三张 `set_texture` | 每个桶只 `set_pipeline` + `set_texture` 一次 |
| 一万根同型号柱 = 一万次绑定 | 一万根柱 = 1 次绑定 + 1 次 `draw_indexed(instance_count = 10000)` |

对照 §6：OSG 用 `StateGraph` 把相同 `StateSet` 的叶子归并到一起，本质是同一件事；Tamias 用哈希桶 + 展平清单做等价归并，不建状态图。

**注意：分桶的前提是「顺序无关」。** 不透明可以随便排；半透明不行，所以 `transparent = true` 的批次绕过桶、立即 flush（见 §8）。

### 第二层：线程 / 上下文对象切换

| 场景 | 规则 | 代码依据 |
|---|---|---|
| UI 与 GPU | UI 线程**从不**直接调 Vulkan / OpenGL。绘制全在 `RenderThread` 上，命令走 channel 投递 | [RENDERING.md](RENDERING.md) §4 |
| Vulkan 多视口 | 相同后端 + 相同校验设置可以**共用一个 Device、一条渲染线程**，每个视口一个 channel | `shares_execution_thread_with` |
| OpenGL 多视口 | **从不共用**。每个 GL 视口自己的上下文、自己的线程 | 上下文是线程局部的，共享会踩绑定 |
| 上传 | 必须在持有 `RHIDevice` 的那条线程上创建 buffer / texture，所以走任务队列 | `RenderThread::upload_mesh` |

OpenGL 的 `wglMakeCurrent` 就是一次真正的「上下文切换」——切了它，之前绑定的 VAO / program / texture 全部失效。**一个视口一个线程一个上下文、线程内不切换**，是这里最重要的一条纪律。Windows 上还有个细节：子 HWND 必须在 UI 线程先建好，再交给渲染线程当交换链表面，否则会和 Qt 死锁（[OPENGL.md](OPENGL.md) §1）。

### 第三层（未做）：命令层的复用

VSG 那种「录好命令缓冲之后直接重放」目前没有：`RecordCommands` 每帧重新遍历。要做的话收益点很明确 —— **代次没变、相机没变的帧，整棵树的录制结果可以直接复用**。这是后续优化，不是现在的瓶颈。

---

## 12. RHI 基于什么设计

### 形状借自现代显式 API

接口在 [device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)，动词几乎一对一对应 Vulkan / D3D12 / Metal 的概念：

```
RHIDevice::create_buffer / create_texture / create_shader_module
           create_pipeline / create_command_list / create_swap_chain / create_fence
           begin_frame → execute(command_list) → end_frame → wait_idle

CommandList::begin / end / begin_render_pass / end_render_pass
             set_pipeline / set_vertex_buffer / set_instance_buffer / set_index_buffer
             set_push_constants / set_texture / draw_indexed / set_viewport / set_scissor
```

几个明显来自 Vulkan 的设计：

| 特征 | 来源 |
|---|---|
| `begin_frame` / `end_frame` + 显式交换链 | Vulkan 的 acquire / present 流程 |
| `PipelineState` 是不透明对象（预创建、可复用） | Vulkan `VkPipeline` / D3D12 PSO |
| `set_push_constants` | Vulkan push constant（OpenGL 侧落到 UBO binding 0） |
| `begin_render_pass` / `end_render_pass` | Vulkan dynamic rendering |
| `clip_space_correction_matrix()` | Vulkan 的 NDC 是 Y 向下、Z 从 0 到 1；数学仍按 OpenGL 习惯算，最后乘这个矩阵校正 |
| `set_instance_buffer` + `PipelineDesc::instanced` | 顶点 instance rate，四个后端都有等价物 |

### 词汇表却被刻意裁到最小

**RHI 不是「Vulkan 的 C++ 包装」**，它只保留「四个后端都能实现」的动词（Vulkan / OpenGL / WebGL / WebGPU）：

- 没有 descriptor set、没有 bindless、没有 pipeline layout、没有 memory barrier、没有 subpass —— 这些都没有跨后端等价物；
- `Texture::write_subresource(mip, layer, data)` 是唯一的多级 / 多层入口，够 cubemap 和 mip 用就行；
- 线框（`PipelineDesc::wireframe`）在 GL 上走 polygon mode，在 WebGL 上没有，能力差异由后端决定。

### 为什么这样切

绘制代码（`draw_channel`）里**没有 `#ifdef VULKAN`**。所有差异被挤到三个地方：

1. `clip_space_correction_matrix()` —— 坐标约定；
2. shader 各编一份（`*.spv` / `*.gl.spv` / WGSL）；
3. 后端实现内部的资源管理（GL 的 dummy context、WebGPU 的 device 队列）。

这和建模内核那一层是同一个套路，可以并排看（[MODELING-KERNEL.md](MODELING-KERNEL.md) §1）：

| RHI | 建模内核 |
|---|---|
| `rhi/device.h` 定义动词 | `kernel/kernel.h` 定义动词 |
| `BackendModule` + `register_backend()` | `KernelModule` + `register_kernel_backend()` |
| `RHIDevice::create({backend})` | `ModelKernel::create({backend})` |
| `Buffer` / `Texture` 不透明句柄 | `Body` 不透明句柄 + `EdgeId` |
| `rhi/vulkan/`、`rhi/opengl/`、`rhi/webgpu/` | `modeling/occt/`、`modeling/truck/` |
| 后端能力不同就降级（WebGL 没线框） | `KernelCapabilities`，UI 按能力灰按钮 |

**一句话：形状学 Vulkan，词汇表按「四个后端都能实现的最小集合」裁剪，差异全部落在后端。**

---

## 13. 插件里的 C# 事件怎么通信

### 三条通道，方向不同

```
宿主（C++）                          插件（C#，独立 AssemblyLoadContext）
   │  ① Bootstrap.Initialize(api*, pluginsDir)         启动、注册命令
   ├──────────────────────────────────────────────►
   │  ② Bootstrap.Invoke("my.command")                 用户点按钮 → 跑插件动作
   ├──────────────────────────────────────────────►
   │  ③ Bootstrap.PointInputCompleted(id, pts, n, st)  异步拾点完成
   ├──────────────────────────────────────────────►
   │
   │  ④ HostApi 里的函数指针（C ABI 结构体 v5）         插件回调宿主
   ◄──────────────────────────────────────────────  host.Log / Dispatch / SetSelection …
```

### ① 宿主 → C#：函数指针，不是导出 DLL

C++ 侧用 `hostfxr` 把 .NET 运行时**装进本进程**，再用 `load_assembly_and_get_function_pointer` 取入口（[csharp_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.cpp)）：

```cpp
using InitFn  = int (*)(HostApi* api, const char* plugins_dir);
using InvokeFn = int (*)(const char* command_id);
using PointInputCompletedFn = int (*)(std::uint64_t request_id, const HostPickPoint* points,
                                      std::int32_t count, std::int32_t status);
```

C# 侧对应的是 `[UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]` 的静态方法（[Bootstrap.cs](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/Bootstrap.cs)）。**没有 COM、没有托管导出库、没有中间互操作层**，就是 C 函数指针。

### ② ④ C# → 宿主：一个 C ABI 结构体

`HostApi`（[host_api.h](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)）是一张函数指针表，`void* context` 指向宿主自己的 `PluginHost`：

```c
struct HostApi {
  int32_t abi_version;   // = kHostApiVersion，当前 5
  void* context;
  void (*log)(void*, int32_t level, const char* utf8);
  int32_t (*dispatch)(void*, const char* command, const char* args_utf8);
  int32_t (*set_selection)(void*, const uint64_t* ids, int32_t count);
  int32_t (*begin_point_input)(void*, uint64_t request_id, ...);
  int32_t (*show_dialog)(void*, int32_t kind, int32_t buttons, const char* spec,
                         char* out, int32_t cap);
  /* 还有 document_name / entity_* / selection_* / register_command
     / register_plugin / cancel_point_input */
};
```

纪律（写在注释里，改动时必须守）：

- **只能在末尾追加字段**，中间插入会让老插件按错偏移读函数指针；布局一变就 `kHostApiVersion + 1`；
- 版本不匹配直接拒签：C# 侧 `if (api.AbiVersion != 5) return -2;`；
- 字符串统一 `char* + cap` 的 UTF-8 输入 / 输出，不做所有权转移；
- `HostApi.cs` 里对应的是 `IntPtr` 字段 + `[UnmanagedFunctionPointer(CallingConvention.Cdecl)]` 委托，用 `Marshal.PtrToStructure<HostApi>` 读进来。

### 异步拾点：靠 `request_id` 认领回调

插件调 `BeginPointInput(options, callback)` 时，宿主生成一个 `request_id`，之后鼠标 / 工作平面 / 吸附 / Esc 取消**全由宿主管**；点够了（或取消）就回调 `PointInputCompleted(request_id, points, count, status)`，C# 侧用 `request_id` 找回原来的 `callback`，把原始 `NativePickPoint` 数组（含 `EntityId`）解成 `PickPoint` 列表。

**回调不在 C++ 的调用栈上返回，而是从宿主事件循环里发出来的。** 这就是「C# 事件」的实际形态：不是 `event` / 委托订阅，而是**带 id 的一次性续延（continuation）**。每个视口只能有一个活动请求；新请求、切文档、Esc 或右键都会取消旧的并回调 `Cancelled = true`（[plugin/develop.md](plugin/develop.md) §3）。

### 和宿主自身事件的关系

宿主内部的事件是另一套：`HostEvent{DocumentChanged, SelectionChanged, ToolChanged, StatusMessage}`（[host_event.h](https://github.com/terry-chao/tamias/blob/main/src/host/host_event.h)），由 `Session::notify` 发出，壳把它翻译成 Qt 信号（桌面）或 JS 回调（WASM）。**插件不订阅 Qt，也拿不到 `Document`**，只能通过 `HostApi` 做只读查询 + `dispatch` 已注册命令。

### 几个必须知道的坑

| 坑 | 事实 |
|---|---|
| CoreCLR 卸不掉 | `hostfxr_close` 只丢上下文，运行时留在进程里，GC / finalizer 线程会和 native 析构打架。所以 `shutdown()` **故意把 hostfxr 句柄置空而不卸载**（注释在 `csharp_runtime.cpp`） |
| 改 C# 不热重载 | ALC 是 `isCollectible: false`，改完要重新 publish + 重启 |
| 异常不穿边界 | `Bootstrap.Invoke` / `Initialize` 全部 `try/catch`，异常转成状态栏日志 + 错误码，不崩进程 |
| API 版本 | `Tamias.Api.dll` 不许拷进 `plugins/`，否则两份接口类型对不上 |
| 从 C# 改文档 | 必须 `host.Dispatch("delete_entity", ...)`，不能在 C# 里自己 new 实体 |

---

## 14. Pick 的容差怎么算

拾取不是一个算法，是**三种**，容差含义各不相同（[picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp)）。

### ① 实体三角面：没有容差，只有精确求交

`intersect_triangle` 是 Möller–Trumbore（[math.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/math.h)）：

```cpp
det = dot(e1, cross(ray.direction, e2));
if (|det| < 1e-6)  return false;   // 射线与三角平行 → 退化
u ∈ [0,1] 且 v ≥ 0 且 u+v ≤ 1       // 重心坐标落在三角内
if (t < 1e-6)      return false;   // 交点在射线原点之后
```

容差就是这两个 `1e-6`：一个是「判定平行」的阈值，一个是「别把起点算成命中」的自交保护。**没有像素容差、没有半径膨胀。** 点到就是点到，点不到就是点不到 —— 三维实体是靠轮廓认的，用户不会去点一个 3 像素宽的边。

### ② 草图折线 / 线框：世界空间半径

`MeshCpu.line_list` 或草图实体走另一条路：把线当成**有粗细的圆柱**做粗拾取。

```cpp
constexpr float kSketchPickRadius = 0.03f;   // math.h:370
intersect_segment(local_ray, a, b, kSketchPickRadius, hit_t);
```

`intersect_segment` 求的是「射线与线段的最近点距离 ≤ radius」—— 先解两条直线的最近点参数（含退化 / 平行分支），把参数 `s` 夹到 `[0,1]`，再比较实际距离。命中判据是距离，不是像素。

几个关键点：

- **0.03 是本地 / 世界单位，不是像素**。`to_local_ray` 只做「旋转 + 平移」的逆变换（`Rᵀ(p − t)`），并要求场景变换是刚体（无缩放），所以刚体保距、交点参数 `t` 不变，半径也就等价于世界单位（注释写明了这个前提）；
- 包围盒会按同样厚度膨胀（[curve_geom.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/curve_geom.cpp) 里 `kSketchPickRadius` 那处），避免轴对齐线段退化成零厚度的盒子；
- 闭合曲线（圆 / 弧）统一按折线段的集合处理。

**为什么只有草图给半径？** 因为草图线在屏幕上可能只有 1–2 像素宽，用「零容差」几乎点不中。这是**手感**决定的，不是数学决定的。

### ③ 轴网：屏幕像素容差

轴网根本不进三维求交，而是「投影到屏幕后比距离」（`pick_grid_axis_on_screen`）：

```cpp
const float dist = distance_to_segment_2d(px, py, ax, ay, bx, by);  // 点到线段，屏幕像素
if (dist <= tol_pixels && dist < best_dist) { … }                   // tol_pixels = 8.0f
```

容差 `kGridPickPixels = 8.f`（[document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp)），**8 个屏幕像素**。两个端点都要投影成功（跑到相机后面就跳过这一帧的判距）。同一个函数也被轴网框选复用。

| 拾取对象 | 空间 | 容差 | 好处 |
|---|---|---|---|
| 实体三角面 | 世界 | 无（1e-6 保护） | 精确、稳定 ID |
| 草图线段 | 世界 | 半径 0.03 | 细线可点中 |
| 轴网 | 屏幕像素 | 8 px | 缩小 / 远景时和肉眼一致 |

### 顺带两个「不是容差但形似」的阈值

- **点击 vs 拖动**：按下到抬起位移 `manhattanLength() < 4` 才算「点一下」，否则当框选；阈值 4 像素。
- **插件拾点**：同样有 4 像素阈值，之前有个 bug 是「点击落点被 4px 阈值丢弃」，已修（git log `fix(wasm): 修点击落点被 4px 阈值丢弃`）。

---

## 15. Pick 怎么找到最前面的实体

### 从鼠标到世界射线

```cpp
Ray camera_ray(camera, aspect, mouse_x, mouse_y, width, height);
```

先把像素映射到 NDC，再用投影矩阵的对角元反解视口平面上的偏移，最后按正交 / 透视分两支：

```cpp
const float ox = ndc_x / proj(0, 0);
const float oy = ndc_y / proj(1, 1);
// 透视：origin = eye,               direction = normalize(right*ox + up*oy + forward)
// 正交：origin = eye + right*ox + up*oy, direction = forward
```

### 二级加速：物体级 BVH，不是三角级

`Bvh::build`（[picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp)）只用**节点级世界包围盒**：

1. 收集所有 `mesh_asset_id != 0` 且 `world_bounds.valid()` 的节点 id；
2. 递归：合并这批节点的 AABB → 取**最长轴** → 用 `std::nth_element` 按中心坐标中位数划分；
3. 区间只剩一个 id 时成为叶子，`scene_node_id` 记下它。

**叶子是一个场景节点，不是三角形。** 所以树很浅、很便宜，但叶子内部要自己遍历三角。

### 怎么保证取到「最前面」

`closest_hit` 是显式栈的深度优先（不用递归，避免爆栈）：

```cpp
std::optional<PickHit> best;  float best_t = FLT_MAX;
while (!stack.empty()) {
  // 1. 盒子都没打中，或者盒子入口已经比当前最优更远 → 整棵子树剪掉
  if (!intersect_aabb(ray, node.bounds, t_box) || t_box > best_t) continue;
  // 2. 叶子：逆变换到局部空间，逐个三角求交
  if (intersect_triangle(local_ray, v0, v1, v2, hit_t) && hit_t < best_t) {
    best_t = hit_t;  best = PickHit{sn->id, t / 3, hit_t};
  }
}
```

「最前面」就是 **射线参数 `t` 最小**。因为场景变换是刚体（无缩放），局部空间算出来的 `t` 和世界空间一致，不需要换算（`to_local_ray` 的注释专门说明了这一点）。

三条剪枝一起来，才让它比「遍历所有三角形」快：

1. **包围盒剪枝**：`intersect_aabb` 不中 → 整棵子树跳过；
2. **距离剪枝**：`t_box > best_t` → 这个盒子在已知最优后面，跳过；
3. **叶子内更新**：只有 `hit_t < best_t` 才覆盖结果。

返回 `PickHit{node_id, triangle_index, t}` —— 它带 `triangle_index`，所以调用方还能继续做「面级 / 边级」的后续判定。

### 还有两个必答的细节

**隐藏的东西不参与拾取。** 视口传了 `accept` 谓词：

```cpp
bvh_.closest_hit(ray, *document_, [this](std::uint64_t id) { return node_visible_in_view(id); })
```

隐藏楼层 / 被 isolate 掉的构件在叶子里就被否掉，不会「点到一个看不见的东西」。

**拾取用的是哪一档网格？** `doc.resolved_mesh(sn->mesh_asset_id)`。它按 `close → work → coarse → 原网` 找第一个顶点非空的资产（[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp)），也就是**绝不用 L0 盒当拾取体**。否则远处点一个盒子会命中「整个包围盒」，而用户看到的是个粗模型。

### 已知上限

物体级 BVH 只解决「几十万构件里找到那个构件」，不解决「一个 5000 万三角的零件内部怎么快点」。那种情况下叶子里的线性遍历仍会卡。下一刀是按 `MeshCpu.faces`（面范围 + 面 AABB）或 cluster 先剪枝 —— 结构已经存好了，只是还没用（[MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) §6）。

---

## 16. 旋转绕谁转

### 默认：绕相机自己的 target，不是鼠标中心

相机是 **Y-up 转盘相机**（[camera.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/camera.h)）。旋转只改两个角度：

```cpp
void orbit(float dyaw, float dpitch) { set_yaw_pitch(yaw_ + dyaw, pitch_ + dpitch); }
Vec3 eye_position() const { return target_ + Vec3{distance*cp*sy, distance*sp, distance*cp*cy}; }
```

`eye` 永远由 `target_ + 球坐标` 算出，所以**旋转中心恒为 `target_`**，鼠标位置完全不参与 orbit。

### `target_` 由谁决定

| 触发 | target 变成 | 代码 |
|---|---|---|
| 视口初始化 | 文档包围盒中心（空文档用单位盒） | `camera_.frame_aabb(document_->bounds() …)` |
| 全览 / Frame All | 文档包围盒中心 | `frame_scene()` |
| 定位某个构件 | 该构件的 `world_bounds` 中心 | `frame_node(node_id)` |
| 定位选中集 | 选中实体的**并盒**中心 | 遍历选中项合并 AABB 后 `frame_aabb(box)` |
| 切楼层 / 平面视图 | 该楼层高度范围内的包围盒（把高度压到楼层，平面视图下高度不参与投影） | `open_floor_view(floor_index)` |
| 打开文档 | 保存的 `ViewportState.target` | `apply_viewport_state` |

所以「鼠标中心 / 场景中心 / 选中实体中心」其实是三种**命令**，不是三种鼠标模式，最后都落到同一个 `target_` 上。

### 鼠标唯一参与的地方：滚轮聚焦

设置里打开 `zoom_to_mouse_position` 后，滚轮会先取光标下的世界点，再 dolly 并把 target 向该点收拢，让那个点在屏幕上不动（[camera_controller.cpp](https://github.com/terry-chao/tamias/blob/main/src/host/camera_controller.cpp)）：

```cpp
void CameraController::dolly_to_focus(float factor, const Vec3& focus) {
  const Vec3 old_target = camera_.target();
  camera_.dolly(factor);
  camera_.set_target(focus + (old_target - focus) * factor);
}
```

换句话说：**缩放可以「以鼠标为中心」，旋转不行。** 旋转永远绕 target。

### 完整交互表

| 操作 | 效果 |
|---|---|
| 左键拖动空白 | 框选（位移 ≥ 4px） |
| 左键点构件 | 选择（Shift 加 / 减选） |
| 左键拖夹点 | 拖拽编辑 |
| 中键拖动 | **orbit**（平面视图下或按住 Shift → pan） |
| 右键拖动 | pan |
| 右键点一下 | 上下文菜单 / 提交折线 / 取消工具 |
| 滚轮 | dolly（可选以光标为中心） |
| 双击 | 提交折线 / 平面视图下适配窗口 |

### 为什么不做「以鼠标为中心旋转」

1. 需要每帧把鼠标下的世界点重新投影回去，目标点会漂（用户感觉画面在「滑」）；
2. 和「框选 / 定位选中 / 平面视图」几套交互抢同一个手势空间；
3. Blender / glTF / 大多数 CAD 都绕 target 转，`frame_scene` / `frame_node` 已经把「我想绕它转」表达清楚了。

---

## 17. 绘制流程：事件还是状态机

**都不是单一答案：输入是「优先级链 + 命令状态机」，绘制是「清单 + 两遍录制」。**

### 输入侧：两层

**第一层 —— 视口里的优先级链**（不是通用状态机，是明确的 if 顺序，写在 `mousePressEvent` / `mouseMoveEvent` 开头）：

```
pending_grid_            落位轴网
  → plugin_point_input_  插件拾点
    → command_system_.has_pending()  正在画的东西要吃这个点
      → gripping_        拖夹点
        → box_selecting_ 框选
          → mmb_nav_ / panning_      相机
            → 都不命中：选择 / 清空选择
```

「谁吃掉这个事件」由这个顺序决定。位置（`last_mouse_`、`press_mouse_`、`press_hit_`）就是这一层的状态。

**第二层 —— 命令状态机**（[command_system.h](https://github.com/terry-chao/tamias/blob/main/src/command/command_system.h)）：

```
dispatch(name, args)   建出 pending_（未执行）
   ↓ feed_point(point, picked_entity_id)   喂一个交互点，返回「完成了吗」
   ↓ hover(point, picked)                  只更新预览，不提交
   ↓ confirm()                             Enter / 双击 / 右键点一下
   ↓ execute() → push 到 undo 栈
cancel()               丢弃 pending_
```

所以「画一面墙」不是一段 if-else，而是**一条命令对象的状态推进**。好处：

- 所有编辑进同一个 undo 栈（`CommandStack`）；
- 命令行、Ribbon、插件 `host.Dispatch(...)` 走的是**同一条** `dispatch` 路径；
- 视口只需要知道「有没有 pending、要不要确认」，不需要认识「墙」。

**`ToolMode` 是什么？** 只是一张「按钮 → 命令名」的表（`dispatch_tool_command`），加上面板武装（`arm_create` 把厚度 / 高度等参数先 dispatch 进去）。它**不是状态机**，只是 UI 的当前选中项，用来决定「画完一个之后要不要再武装一次」（`rearm_tool`）。

### 从点到像素：一次完整绘制

```
① Qt 事件（mouseMove / mousePress / wheel / key）
      ↓ DocumentViewport（优先级链）
② 算世界点：cursor_ground_position / cursor_world_position（射线 × 工作平面 / 捕捉）
      ↓ 需要命中的话先打 BVH（见 §15）
③ 改文档：command_system_.feed_point / confirm → 命令 execute → Document 变了
      ↓ 命令完成后：resync_all_meshes() + rebuild_bvh() + emit document_changed
④ 提交一帧：submit_current_frame()
      生成 SceneDrawItem 清单（render_items(视锥)）+ 相机 + overlay 数据
      ↓ channel_->submit(frame)   ← mailbox：只保留最新一帧
⑤ RenderThread（另一条线程）
      代次没变 → 复用留存 RenderNode 树；变了 → 全量重建或脏标记增量更新
      ↓ 第一遍不透明 + 第二遍半透明（RecordCommands 按 BatchKey 分桶）
⑥ present
```

`submit_current_frame` 里的 **mailbox 语义**很关键：UI 线程永远不等渲染线程，中间的帧直接丢掉。这是「宁可跳帧也不要排队卡顿」的取舍。

### 为什么不统一用「事件总线」

因为编辑是**有状态、可撤销、可被插件调用**的：命令必须是数据（`Command` 对象），不能是「一堆信号处理器的副产物」。现在的分工是：

- **视口**：把 Qt 事件翻译成「世界点 + 命中 id」；
- **命令系统**：状态推进 + undo；
- **HostEvent**：只向外广播「文档变了 / 选择变了 / 工具变了」，给状态栏、属性面板、插件用。

---

## 18. 计时工具怎么拿到调用栈

### 两个工具，一个埋点

| 工具 | 面向 | 能不能看调用栈 |
|---|---|---|
| **TimingPanel**（自研，Release 也带） | 用户 / QA / 现场 | **能**，看的是自研的「嵌套区间树」 |
| **Tracy**（只在 Debug / RelWithDebInfo 链接） | 开发 | 能，而且能采样真实调用栈 |

埋点只写一次：`TAMIAS_TIMING_SCOPE("名字", 类别)` 同时喂两边（[PROFILING.md](PROFILING.md)）。

### 自研那半：`thread_local` 开区间栈

核心在 [timing_session.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/profile/timing_session.cpp)：

```cpp
thread_local std::vector<int> t_open_events;   // 每条线程一份「还没结束的事件」栈
```

**构造（进入作用域）**：

```cpp
TimingScope::TimingScope(name, category) {
  if (!session.is_recording() || !session.category_enabled(category)) return;  // 几乎零开销
  generation_ = session.generation();
  index_ = session.begin_event(name, category);
}
```

`begin_event` 里做三件事，**父节点就是这样确定的**：

```cpp
event.start_us  = now - origin;                                  // 相对录制起点的微秒
event.thread_id = current_thread_id();
event.parent    = t_open_events.empty() ? -1 : t_open_events.back();   // ← 栈顶就是父
const int index = events_.size();
events_.push_back(event);
t_open_events.push_back(index);                                  // ← 自己成为新的栈顶
```

**析构（离开作用域）**：

```cpp
event.duration_us = end_rel - event.start_us;
if (!t_open_events.empty() && t_open_events.back() == index) t_open_events.pop_back();
```

所以「函数调用栈」不是断点回溯、不需要调试符号、不需要帧指针 —— 它是 **RAII 作用域 + 每线程开区间栈**，把「谁在谁里面」直接记成了 `parent` 索引。`TimingEvent` 因此天然是一棵树：

```cpp
struct TimingEvent {
  std::string name;  TimingCategory category;
  std::uint64_t start_us, duration_us, thread_id;
  int parent;
};
```

配套的三件事：

| 机制 | 解决什么 |
|---|---|
| `thread_id` | 每条线程一棵独立的树（`ui` / `render` / `tess`），也方便按线程画泳道 |
| `generation_` | 录制中途 stop / clear 时，析构里的旧 `index_` 不会写到新一批事件里（构造和析构各校验一次） |
| `exclusive_durations()` | 自身耗时 = 本事件时长 − 所有直接子事件之和（叶子等于总时长），算的是「这层真正花了多少」 |

输出：[timing_xml_writer.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/profile/timing_xml_writer.cpp) 把树写成 XML，[timing_timeline_widget.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/timing_timeline_widget.cpp) 画时间线，类别芯片控制过滤。

### 这套做法的边界

**它只能告诉你「你自己埋了点的地方」**：

- 没埋点的函数不在树上（哪怕它很慢）；
- 名字是 `string_view`，但 `begin_event` 里 `assign` 成 `std::string` —— 有分配，别塞进每帧几万次的内循环；
- 字面量用 `TAMIAS_TIMING_SCOPE`，运行时字符串用 `TAMIAS_TIMING_SCOPE_DYNAMIC`；
- 看不到内核 / 驱动 / 系统调用花在哪；
- 编译期关掉 Tracy 时两个宏退化成 `((void)0)`，但 TimingPanel 那一半照常工作。

### 想要「真实调用栈」怎么办

三种，按代价排：

1. **Tracy 的采样**：`RelWithDebInfo`（`/Zi /Ob1`）已经够用，Release 不适合采样（[PROFILING.md](PROFILING.md) §6）；
2. **手动加埋点**：在怀疑的路径上补 `TAMIAS_TIMING_SCOPE`，因为埋点是「一次写、两边都收到」；
3. **平台工具**：Windows 用 ETW / WPA，Linux 用 `perf`，看内核与驱动栈。

Tamias 现在没做**内存**和**锁**的埋点（需要重载全局 `operator new/delete`、把 `mutex` 换成 `TracyLockable`），这两项都在后续清单里。

---

## 19. 文字渲染、闪烁与显示顺序

### 现状：三维视口里没有 GPU 文字

先把事实说清，免得找错地方：

| 位置 | 文字怎么画 |
|---|---|
| 三维视口的坐标读数 / 统计（draw / tri / gpu MB / tess） | **Qt 的叠加 `QLabel`**（`coord_label_`，`sync_coord_readout()` 拼串后 `setText`） |
| ViewCube 的上 / 下 / 左 / 右 / 前 / 后 | Qt `QPainter::drawText` 画在控件上（[view_cube_widget.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/view_cube_widget.cpp)） |
| 二维图纸查看器（DXF / DWF） | 字形轮廓 → `QPainterPath` → 并入批次（见下） |
| 三维场景里的世界空间文字（标注 / 尺寸） | **没有。** 现在不渲染任何世界空间文字 |

所以「文字闪烁 / 显示顺序」这两个问题，在图纸查看器里是真实存在的，在三维视口里因为根本不画世界文字，暂时不会遇到。

### 图纸里的文字怎么实现的（可直接照抄的做法）

数据是 `DrawingText{position, height, rotation_deg, text, layer, color}`（[drawing_text.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing_text.h)）；渲染走**字形轮廓**而不是描边字体或位图（[drawing_document.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_document.cpp)）：

```cpp
QFont font;  font.setPixelSize(100);            // 固定 em，靠矩阵缩放
QPainterPath glyphs;  glyphs.addText({0, 0}, font, content);
const double scale = text.height / 70.0;        // DXF 字高 ≈ 大写字母高 ≈ 0.7 em
const QTransform place(cs*scale, sn*scale, sn*scale, -cs*scale, x, y);  // 旋转 + 世界 Y 向上
batch_for(page, layer, color).paths.addPath(place.map(glyphs));
```

好处：

- **任意缩放都是矢量的**，不会糊（对比位图字体）；
- 文字和曲线**共享同一个 `PathBatch`**，键是 `(page, layer, color.rgb())`；
- 线宽用 `pen.setCosmetic(true)`，屏幕恒 1 px，不随缩放变粗。

注意 `paint_vector` 是 `setBrush(Qt::NoBrush)` + 1 px 描边，所以**图纸文字和线条是同一种「发丝线」画法**（字形是空心轮廓），这也正是 CAD 图纸的观感。

### 如果要自己实现「最简单的文字」

按复杂度从低到高，建议从第 2 条开始（够用且不糊）：

1. **位图字体**：一张等宽图集 + 每字符一个 quad。最土、最快写完，缺点是缩放糊。
2. **SDF 字形图集**：离线把每个字形烘成距离场（一张 R8 图集），shader 里 `smoothstep` 定边缘。任意缩放、可加粗描边、一个 pipeline 画完 —— 工业做法。
3. **矢量轮廓**（就是上面图纸用的）：质量最好，但要三角化，每帧顶点多，适合静态文字。

最小实现要处理的东西（少任何一个都会出问题）：

| 项 | 怎么做 |
|---|---|
| 字形缓存 | 键 `(font_id, size, char)`；命中直接取图集 UV |
| 图集打包 | 动态增长的行式 / 矩形打包，别一个字一张贴图 |
| 顶点 | 每字符 4 个顶点，UV 指向图集；世界空间 quad 或屏幕空间 quad |
| 朝向 | 屏幕空间（billboard，永远正对相机）还是贴在世界平面上（会斜视）—— 先想清楚 |
| 深度 | 文字**永远** `depth_test = false`，放 overlay 通道最后画 |
| 排序 | 按插入顺序（稳定）或按优先级；别依赖哈希顺序 |

### 闪烁（flicker）的四个真实原因

| 原因 | 症状 | 修法 |
|---|---|---|
| **Z 冲突** | 文字和它所在的面共面，逐帧 / 逐像素谁赢不定 | 文字 `depth_test = false` 放 overlay；或面用 polygon offset；或直接屏幕空间画 |
| **亚像素抖动** | 静止时字符在帧间左右跳 1 px | 位置对齐到设备像素（`round()` 到整像素 / 0.5 像素），关掉次像素定位；缩放变化后重新对齐 |
| **双缓冲缺失 / 重复清屏** | 先看到背景再看到内容（真·闪） | Qt 控件默认双缓冲；`paintEvent` 里**只 `fillRect` 一次**（[drawing_view.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_view.cpp) 就是这么做的），别在 resize / 子控件里再清一遍 |
| **坐标精度** | 远处文字每帧抖 | 用相机相对坐标（见 §20） |

### 显示顺序（z-order）

**二维图纸**：`paint_vector` 按 `dxf_batches_` 的**创建顺序**画，批次键是 `(页, 图层, 颜色)`，而创建顺序来自 `build_vector_batches` 的遍历顺序 —— 一页里**先加曲线、后加文字**。所以：

- 文字批次排在曲线批次之后 → **文字描边画在线条之上**；
- 顺序由「层 / 颜色首次出现的顺序」决定，不是由文件里的实体顺序决定；
- **同一个批次内部的先后是不可控的** —— 一个 `QPainterPath` 一次 `drawPath`，path 内部没有 z 概念。想控制顺序就得拆批次。

想要严格可控的顺序，三种做法：① 给文字单独批次并最后画；② 按图层号显式排序批次后再画；③ 需要「谁盖谁」的成对元素（比如填充盖网格）必须放进不同批次。

**三维里**：文字必须在 overlay 通道（和轴 / 预览线同级，`depth_test = false`）最后画，否则不是被模型盖住，就是穿透模型（取决于深度设置）。

---

## 20. 大场景里的 float

### 事实：这套管线里几何全是 float32

```cpp
struct Vec3 { float x, y, z; };
struct Mat4 { float m[16]; };     // math.h
struct Vertex { Vec3 position; Vec3 normal; Vec2 uv; Vec3 color; };   // 44 字节，mesh.h
```

顶点、法线、矩阵、push constant、shader 全程 `float`。没有 `double` 版本。

### 精度算一下

float32 尾数 24 位，相对精度 ≈ `2⁻²⁴ ≈ 6e-8`。**能表示的最小间距随量级线性变差**：

| 世界坐标量级 | 最小可分辨间距 | 后果 |
|---|---|---|
| 1e3（1 km） | ~0.00006 m | 无感 |
| 1e5（100 km） | ~0.006 m | 6 mm 抖动，细部开始打架 |
| 1e6 | ~0.06 m | 6 cm 抖动，Z 冲突、网格线断裂 |
| 1e7 | ~0.6 m | 基本没法用 |

厂区级 BIM / 汽车总装经常带测量网坐标（UTM 东坐标 5e5 是常态），**一进场景就已经在第二行了**。

### 四个具体症状

1. **顶点抖动（vertex jitter）**：`view` 矩阵是「先把世界坐标平移到相机原点」，可是**平移之前**精度已经丢了。相机一动，同一个顶点落在不同位置上。
2. **Z 冲突**：深度缓冲精度随 `zfar / znear` 恶化。本仓库的 `set_distance` 把 `znear = distance * 0.001`、`zfar = distance * 50` —— 拉远之后可能是 `0.05 / 500` 这种比例，深度的有效位基本用完了。
3. **地面网格断**：`grid.frag.hlsl` 用 `frac(world_pos.xz / spacing)` 画线。坐标到 1e6 时，`frac` 的输入已经只剩 0.06 的量化步长，`fwidth(coord)` 也一起失效 —— 网格会在远处碎成噪声，而不是「模糊」。**这是 float 精度问题，不是网格参数问题。**
4. **拾取退化**：CPU 的射线求交也是 float，远距离三角形的重心判定会掉精度（`|det| < 1e-6` 直接判退化）。

### 已经在用的对策：图纸模块的原点重定基

`Drawing::normalize_origin()`（[drawing.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing.h)）就是标准解法，注释写得很清楚：

> 把整体平移成靠近原点（DXF 常用大坐标，float 精度会掉），被减掉的量记在 `world_origin()`，显示绝对坐标时加回去。

也就是说：**内部用「局部原点附近的小坐标」算，对外显示时加回偏移。** 这也解释了为什么状态栏坐标是 `cursor_world_ + world_origin()`。

### 三维侧该做的（尚未做）

| 手段 | 做法 | 代价 |
|---|---|---|
| **大世界原点（floating origin）** | 相机 / 首个构件当原点，几何和变换全部相机相对；CPU 上保留绝对坐标（必要时用 double），**提交前减去原点** | 要和 BVH、包围盒、拾取同步改，否则点选会偏 |
| **相机相对渲染** | view / model 用 double 乘完再转 float 送 shader；shader 里只有相机相对坐标 | 每帧多几十次 double 矩阵乘，可忽略 |
| **反向 Z + float 深度** | `znear / zfar` 交换、深度比较反着来；或直接上 `D32_SFLOAT` + reverse-Z | 要改所有管线与投影矩阵 |
| **固定 znear** | 别让 `znear` 跟着 `distance` 变（现在是 `distance * 0.001`） | 近处裁剪会变，需要权衡 |
| **网格 shader 用相机相对坐标** | 去掉 `world_pos` 绝对值，改传「相对原点坐标 + 原点在 shader 里的偏移」 | 会改 `grid.frag.hlsl` |

**顺序建议**：先做「固定 znear + 反向 Z」（局部改、收益大、不动数据结构），再做相机相对渲染与 floating origin（要动拾取和包围盒，必须一起做）。

---

## 21. LOD 的数据源在哪

### 结论

**数据源在 `Document` 的 `TessCache`（一张文档级表），不是每个模型自己藏一份。** 每帧快照给渲染线程，档次选择在渲染线程按屏幕误差做，缺档再回文档排队离散。

### 五步链路

```
① Document::TessCache          geometry_id → LodMeshSet{coarse, work, close}
        ↓ 每帧快照
② FrameSubmission.lod_sets     （document.cpp: graph.lod_sets = tess_cache_.snapshot()）
        ↓
③ RecordCommands（渲染线程）   projected_aabb_pixels → select_mesh_lod（滞回）
        │                      拿不到档位 → lod_requests.push_back(LodRequest)
        ↓
④ RenderThread::pending_lod_requests_  → 视口 take_lod_requests()
        ↓                              → Document::enqueue_lod_request()
        ↓                              → TessWorker（后台单线程，OCCT 不上 UI / 渲染线程）
⑤ 完成 → intern_mesh + tess_cache_.bind(geometry_id, lod, asset_id)
        ↓ 下一帧的 snapshot 里就有这一档
```

关键文件和类型：

| 东西 | 是什么 |
|---|---|
| [tess_cache.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/tess_cache.h) | `geometry_id → LodMeshSet`，**LOD 的唯一真相源**；同时记 `pending` 集合避免重复排队 |
| [lod_mesh_set.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/lod_mesh_set.h) | 三条资产 id：`coarse` / `work` / `close`（`Box` 不进表，因为是共享单位盒） |
| [mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/mesh_lod.h) | 档次阈值与滞回：`< 4 px` → Box，`< 80 px` → Coarse，否则 Work；升档 ×1.25、降档 ×0.8 |
| [lod_request.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/lod_request.h) | 渲染线程要的「缺哪一档」，可哈希、可去重 |
| [tess_worker.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/tess_worker.h) | 后台离散队列；桌面单 worker 线程，WASM 靠 `pump()` 每帧跑几个 |

### 三个容易猜错的点

**① L0 盒不属于任何模型。** 它是渲染线程里**唯一一份**单位盒网格（`lod_box_mesh_` / `lod_box_gpu_id_`），世界矩阵由 `lod_box_world_matrix(bounds) = translate(center) * scale(extent)` 现算（[mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/mesh_lod.h)）。所以「远处十万个构件退化成一个盒子」的实际代价是**十万个实例、一份网格、一次 draw 左右**。

**② 档次选择不在文档侧，在录制侧。** `select_mesh_lod(projected_px, previous, selected, lines)` 需要 `eye_position` / `fovy` / `framebuffer_height`，这些都是**每帧、每 channel** 的信息。写进 `SceneNode` 或 `render_items()` 的话，相机一动就会把「留存树」整棵打脏（[MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) G3 专门解释了这条）。所以滞回状态也存在 channel 上（`ChannelState.lod_by_node`），不是文档上。

**③ 拾取和渲染用的不是同一档。** 渲染可以退到 L0 盒（4 px），但拾取走 `Document::resolved_mesh()` —— `close → work → coarse → 原网`，**跳过 Box**（[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp)）。

### 为什么不做「每个模型自己拿」

| 每模型自带 LOD | 文档级 TessCache |
|---|---|
| 一万根相同柱各存三档 → 三万份网格，实例化失效 | 一份几何 + 一份三档，实例照旧共享 |
| 显存淘汰要逐个问模型 | `ResidentCache` 一处 LRU，按字节预算淘汰 |
| 换相机改模型 → 脏标记 + 序列化都可能被污染 | 相机只和渲染线程有关，文档不脏 |
| 跨视口各自离散（同几何离散两遍） | 文档级 pending 去重，两个视口共享结果 |

改参数的失效路径也是集中处理的：`invalidate_geometry_lods(geometry_id)` → 清 `TessCache` 条目 → `TessWorker::cancel_geometry` → 对不再被引用的档位 `drop_unref_mesh`。

---

## 22. sketch 模块：这一版不做

**范围声明：本版不做（不实现、不深化）草图（sketch）模块。**

### 明确不做的边界

| 不做 | 含义 |
|---|---|
| 约束求解器 | 不做 coincident / tangent / parallel / 尺寸约束的方程求解，不做自由度分析 |
| 草图平面 | 不做「选一个面 / 基准面建立草图」这套工作流 |
| 约束驱动的重算 | 不做「改一个尺寸 → 整张草图重解 → 特征树重算」 |
| 草图的图元编辑 | 不做约束下的拖拽 / 修剪 / 延伸 / 偏移 |

### 但仓库里已经有的东西（别混淆）

下面这些**已经存在**，但都不是「草图求解器」，不要读错：

| 已有物 | 是什么 | 不是什么 |
|---|---|---|
| `FeatureKind::Line / Polyline / CircleWire / Arc / Bezier / BSpline / Nurbs`（[feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h)） | **参数化曲线特征**：点序列 / 控制点直接存在 `params` 里 | 不是带约束的草图几何 |
| `is_sketch_feature(kind)` | 用来判断「这是曲线、不是实体」 | 不代表有草图解算 |
| 曲线拾取半径 `kSketchPickRadius`（§14） | 让细线可点中的手感参数 | 不是几何容差 |
| `select_mesh_lod` 里 `lines → Work` | 草图**永不 LOD**（线退化成盒就没法用了） | —— |
| `Entity::is_sketch_entity()` | 拾取按折线段、网格按 `line_list` 画 | —— |

### 替代路径

没有草图求解器，不等于不能用曲线。现在可以做的：

- **画**：折线 / 直线 / 圆 / 圆弧 / 贝塞尔 / B 样条 / NURBS，逐个按点创建（`create_polyline` / `create_bezier` / `create_arc` / `create_bspline` …），走的是同一条命令状态机（§17）；
- **改**：夹点拖拽、`[` / `]` 调参数（`adjust_selected_param`）、属性面板改参数；
- **存**：曲线是特征（`params` 里的点序列），可撤销、可序列化、改参数可重算；
- **用途**：曲线可以当轮廓参与拉伸（`Extrude` 的 `inputs[0]`），所以「用曲线画出截面再拉伸」这条建模路径是通的。

缺的只是**约束**这一层：现在「两条线垂直」靠你把点摆成垂直，而不是靠约束求解器保证。

---

## 源码锚点

想按代码核对上面的结论，从这里进：

| 主题 | 文件 |
|---|---|
| 顶点 / 网格格式 | [mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h)、[mesh_face_range.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh_face_range.h) |
| 数学 / 拾取几何 | [math.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/math.h)、[camera.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/camera.h) |
| 拾取 | [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h)、[picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp) |
| 文档 / 场景 / LOD 表 | [document.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.h)、[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp)、[scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h)、[tess_cache.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/tess_cache.h) |
| 渲染树 / 录制 / 合批 | [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.h)、[scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp)、[batch_key.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/batch_key.h)、[gpu_instance.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/gpu_instance.h) |
| 渲染线程 / 驻留 / 纹理 | [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h)、[render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp)、[resident_cache.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/resident_cache.h)、[texture_mips.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/texture_mips.cpp) |
| LOD 策略 | [mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/mesh_lod.h)、[lod_mesh_set.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/lod_mesh_set.h)、[lod_request.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/lod_request.h) |
| RHI | [device.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/device.h)、[opengl_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/opengl/opengl_device.cpp)、[vulkan_device.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/rhi/vulkan/vulkan_device.cpp) |
| 造型 / 内核 | [kernel.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/kernel/kernel.h)、[occt_kernel.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt/occt_kernel.cpp)、[occt_shape_ops.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt/occt_shape_ops.cpp)、[feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h)、[tess_worker.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/tess_worker.h) |
| 命令 / 会话 | [command_system.h](https://github.com/terry-chao/tamias/blob/main/src/command/command_system.h)、[session.h](https://github.com/terry-chao/tamias/blob/main/src/host/session.h)、[camera_controller.cpp](https://github.com/terry-chao/tamias/blob/main/src/host/camera_controller.cpp) |
| 视口 | [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp)、[drawing_view.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_view.cpp)、[view_cube_widget.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/view_cube_widget.cpp) |
| 图纸 / 文字 | [drawing.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing.h)、[drawing_text.h](https://github.com/terry-chao/tamias/blob/main/src/engine/drawing/drawing_text.h)、[drawing_document.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/drawing_document.cpp) |
| 性能分析 | [timing_session.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/profile/timing_session.cpp)、[timing_scope.h](https://github.com/terry-chao/tamias/blob/main/src/engine/profile/timing_scope.h)、[timing_event.h](https://github.com/terry-chao/tamias/blob/main/src/engine/profile/timing_event.h) |
| 插件 | [host_api.h](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)、[plugin_host.h](https://github.com/terry-chao/tamias/blob/main/src/plugin/plugin_host.h)、[csharp_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.cpp)、[Bootstrap.cs](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/Bootstrap.cs)、[HostApi.cs](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Api/HostApi.cs) |

相关文档：[超大规模三角](MASSIVE-GEOMETRY.md) · [合批 / Instancing](INSTANCING.md) · [语义树](SCENE-GRAPH.md) · [渲染管线](RENDERING.md) · [OpenGL 后端](OPENGL.md) · [WebGPU 后端](WGPU.md) · [视锥剔除](FRUSTUM-CULLING.md) · [建模内核](MODELING-KERNEL.md) · [特征树求值器](FEATURE-TREE-EVALUATOR.md) · [MCAD 管线](MCAD-PIPELINE.md) · [几何边界](ISHAPE-OPS.md) · [参考图纸](DRAWING.md) · [插件开发](plugin/develop.md) · [性能分析](PROFILING.md) · [测试](TESTING.md) · [路线图](ROADMAP.md)
