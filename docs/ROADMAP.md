# Tamias 路线图

> 跨 MCAD / BIM 的**几何查看 + 参数化编辑内核**的定位、架构决策、重点难点与里程碑规划。

**状态**：M0–M6 已完成（M6 = 场景图层级化：语义/渲染分离 + 树 + transform 累加 + 包围盒缓存）。下一步主线 = **参数化编辑内核（P1–P4）**。

---

## 0. 一句话定位

**跨 MCAD / BIM 的几何查看 + 参数化编辑内核** —— Qt 做壳，自研 RHI 做双后端渲染，OCCT 提供 BRep 几何内核，IfcOpenShell（构建在 OCCT 之上）提供 BIM/IFC 的语义 + 几何，中间用一个统一的几何边界（**特征树**）+ 分层场景图把它们串起来。

对标物：**FreeCAD（参数化建模内核，构建在 OCCT 上）+ Navisworks / Solibri（BIM 大模型查看）** 的结合体。

核心价值：**大模型下能流畅地看、选、剖切、分类着色**，并且**能参数化地编辑几何**（改参数 → 重算 → 渲染）。

> 已决定（2026-08）：编辑目标是**参数化编辑**（非仅查看、非直接编辑）。这是 FreeCAD 方向，是长期主线。

---

## 1. 概念澄清：格式的分工

工作格式与交换格式不是一回事。**编辑态用 `.tdoc`（存特征树），交换态用 IFC / STEP / glTF。**

| 格式 | 角色 | 谁负责 |
|---|---|---|
| `.tdoc` | **内部工作文档**：语义场景图 + **特征树（参数化几何）** + 材质 + 视图/会话状态 | 自有 `src/io`（binary_archive） |
| `.ifc` | **导入源 / 导出出口**（建筑交换格式） | IfcOpenShell（读 + 写） |
| `.step/.iges/.brep` | 导入/导出（MCAD，BRep） | OCCT |
| `.obj/.glb` | 导入/导出（通用 mesh） | 自有 loader |

类比：**IFC = PDF（交换/交付），`.tdoc` = docx/psd（工作/编辑）。**

- 编辑永远发生在内部 `Document` 上；IFC / STEP 只是「进/出」的端口。
- 因为要**编辑**，`.tdoc` 存的不再只是三角网，而是**特征树（参数 + 依赖）**——三角网和 BRep 都是可从特征树重算的**缓存**。
- `.tdoc` 是自包含的：打开 IFC 翻译进内部结构后，原 IFC 文件即可丢弃。

---

## 2. 五块技术的真实关系

```
┌────────────── Qt 客户端 (app) ──────────────┐
│  属性面板 / 大纲树 / 建模工具 / 命令 / 撤销   │
├────────────── 语义场景图 (document/scene) ────┤
│  层级 · IFC 属性 · 材质引用 · 变换 · 脏标记    │
├────────────── 几何层 (modeling) ─────────────┤
│  特征树（参数化配方）· 求值器 → BRep · 三角网缓存 │
├────────────── 渲染场景图 (render) ────────────┤
│  合批 · BVH · draw list · LOD · 裁剪          │
├────────────── RHI (engine/rhi) ──────────────┤
│  Vulkan 主 · OpenGL 副（同一抽象）            │
├────────────── 几何边界 (IShapeOps) ──────────┤
│  ┌──────────────┐   ┌──────────────────────┐ │
│  │ OCCT (STEP/   │   │ IfcOpenShell (IFC)    │ │
│  │  IGES/BREP)   │   │  └─ 内部用同一 OCCT ──┘ │
│  └──────────────┘   └──────────────────────┘ │
└───────────────────────────────────────────────┘
```

两个最容易误解的点：

1. **IfcOpenShell 不是和 OCCT 并列的，它坐在 OCCT 上面。** IfcOpenShell = `IfcParse`（IFC schema/对象模型/语义）+ `IfcGeom`（几何，依赖 OCCT + Boost）。所以 `IShapeOps` 边界是统一的——OCCT 直接喂 MCAD，IfcOpenShell 把 IFC 转成「语义图 + 几何」喂进来。**不要为 IFC 另开一条几何通道。**

2. **IFC 和 STEP 的本质区别是语义。** STEP 是纯几何（BRep），IFC 有完整的建筑语义：空间结构、类型/实例、属性集（Pset）、材质、GUID、分类。这决定了场景图不能是扁平结构（M6 已解决）。

---

## 3. 核心架构决策

### 决策一：语义场景图 与 渲染场景图 分离 ✅（M6 已落地）

用稳定 ID 关联，不要让一个结构同时干两件事。当前 [scene.h](../src/scene/scene.h) 的 `SceneNode` 已是树（parent/children + local/world transform + 缓存 world bounds），`gpu_mesh_id` 已从语义侧移除、迁到渲染侧（[render_runtime.h](../src/engine/render/render_runtime.h) 的 `asset_to_gpu_`）。

| | 语义场景图（document 侧） | 渲染场景图（render 侧） |
|---|---|---|
| 持有 | IFC GUID / Pset / 层级 / 材质引用 / 变换 | GPU mesh 引用 / batch / BVH / draw list |
| 负责 | 撤销、序列化、大纲树、属性面板、语义查询 | 合批、LOD、裁剪、instancing、绘制 |

### 决策二：几何 = 特征树（第一等公民）+ BRep/三角网（缓存）★ 新增，参数化编辑的地基

因为要参数化编辑，几何的**源头**是「特征树」（参数 + 依赖），不是三角网、也不是纯 BRep：

```
特征树（参数化配方，存进 .tdoc）
   │  求值（Evaluator）
   ▼
BRep（TopoDS_Shape，精确几何，缓存）
   │  tessellate
   ▼
三角网（MeshCpu，渲染用，缓存）
```

- 改参数 → 特征树重算 → 新 BRep → 新三角网 → 渲染更新。BRep 和三角网**都能重建，所以都是缓存**。
- 语义场景图引用「特征树」作为几何资产（替代现在直接引用 `MeshAsset` 三角网的关系）。

---

## 4. 场景图管理：重点难点

M6 已落地项：**层级树、transform 累加、世界包围盒缓存**（`Scene::recompute_world()` 全量重算）。剩余项：

**渲染场景图的实现哲学（已定 = VSG 式）**：渲染侧的「场景图」采用**节点 + 访问者（Visitor）**组织（OpenInventor → OSG → VSG 一脉），但**渲染状态用现代化挂法**——命令图（`StateGroup` + `StateCommands`，对应 Vulkan 命令录制），而非 OSG 的 `StateSet` 隐式继承。关键边界：渲染场景图是**面向绘制的投影**（draw-oriented：drawable + transform + material + 可见性），**不是语义树的复制**——语义树（`Scene`）仍是层级的唯一真相源，渲染侧由它**增量同步**（脏标记）而来。详见 [SCENE-GRAPH.md](SCENE-GRAPH.md)。

1. **实例化（大模型性能的命门）。** IFC 里 IfcWallType / IfcMappedItem 大量复用同一几何。场景图必须支持「一个几何被 N 个节点引用 + 每节点独立 transform / 材质 override」。渲染侧才能 instanced draw 或按 mesh 合批。
2. **语义实体 vs 几何节点分离。** 一个 IFC 元素可能对应多个几何（IfcDoor = 门板 + 门框 + 把手），多个元素也可能共享一个几何。语义节点和几何节点分表，靠关系边连。
3. **增量同步。** 语义侧改一个 transform，不该触发整棵渲染图重建。脏标记 + 增量上传。**参数化编辑会放大这一点**：改一个特征参数，要精准重算「受影响的几何 + 受影响的渲染」，而不是全场景推倒重来。
4. **特征树 → 场景图的双向映射。** 特征树的某个节点改了，哪些场景节点/网格要刷新；反过来选中一个构件，怎么定位到它的特征。这是编辑与场景图的接缝，P1 就要设计清楚。

---

## 5. 渲染：重点难点

现在渲染器是 [render_runtime.h](../src/engine/render/render_runtime.h) 里的 forward + push constant + 单材质（颜色 + 光照方向 + eye/模式），wireframe/shaded/realistic 三模式基本退化。重点难点按优先级排：

1. **大模型可扩展性（第一优先级）。** IFC 动辄 10 万+ 元素、千万级三角。核心指标是 **draw call 数**——绝不能每元素一个 draw。手段：按材质分桶合批、GPU instancing、视锥裁剪 + 空间索引、渐进/流式加载 + LOD。
2. **材质系统。** 现在材质就是一个 push constant 里的 `color[4]`。需要真正的 Material 抽象：PBR metallic-roughness + base color + 纹理 + 透明度，语义对齐 glTF。这要求 RHI 补上真东西——[device.h](../src/engine/rhi/device.h) 里 `create_texture` 还是占位，只有 push constant 没有 descriptor set / UBO / sampler。
3. **截面裁剪（BIM 刚需）。** clip plane 传 shader 逐像素 discard，或 stencil 双面裁剪 + cap 面填充。双后端 RHI 两套管线语义要对齐。
4. **透明度。** 玻璃/幕墙。不透明先画、透明按深度排序、per-material blend 状态。
5. **选择/高亮/分类着色。** 现在是单 `selected` 单色高亮。升级：多选、轮廓高亮、按 IFC 类型/系统/专业分类着色（Appearance Profiler）。
6. **显示模式补全。** realistic 接 PBR + 光照 + 阴影 + AO；补 Hidden Line（隐藏线）模式。
7. **双后端对齐。** Vulkan 和 OpenGL 两套管线行为一致是持续成本点——材质 UBO 布局、纹理格式、clip 语义、instancing。shader 是 HLSL→SPIR-V，GL 端要走 SPIR-V 或另编 GLSL。

---

## 6. 参数化编辑内核：重点难点 ★ 新增主线

这是从「查看器」升级到「编辑器」的核心，也是工程量最大的方向（对标 FreeCAD）。

1. **特征树 + 求值器。** 每个操作 = 一个带参数的节点（Box / Extrude / Fillet / Boolean…），求值器从根到尾算一遍得到 BRep。**改参数 → 只重算下游节点**。
2. **拓扑命名（Topological Naming，最难、最著名的坑）。** 「给这条边倒圆角」引用的是「当前 BRep 里的边 #N」，但改了上游参数后 BRep 重算、边的编号全变，圆角就倒错边了。解法从简单到复杂：**索引法**（按编号，简单但脆）→ **几何法**（按边的位置/方向/相邻面匹配，稳但复杂）。FreeCAD 被此问题折磨多年。
3. **命令化 undo。** 现在 [history.h](../src/document/history.h) 是全量快照，几何编辑多了会重。目标：每条操作（改参数/加特征）是一条可逆命令。
4. **BRep / 特征树序列化。** OCCT 原生支持 `BRepTools::Write/Read` 和 STEP 写出；特征树（参数 + 依赖）要自研序列化进 `.tdoc`。三角网和 BRep 都是缓存，不存或存缓存。
5. **建模 UI。** 选择对象、预览、参数输入的完整交互链——这是「内核」之外的另一个大工程。

### 其他重点难点（非渲染/非建模，但会卡你）

1. **IfcOpenShell 集成（容易翻车）。** `IfcGeom` 针对特定 OCCT 版本编译。现在用 OCCT 7.9.x，**必须让 IfcOpenShell 用同一份 OCCT**，否则同进程两个 OCCT 符号冲突。
2. **IFC 几何导入对齐特征树。** 有了参数化内核后，IFC 的声明式几何（IfcExtrudedAreaSolid 等）**天然可以映射成特征树的节点**（轮廓 + 拉伸），而不是只导入三角网。这是「IFC 编辑」的关键——只有导成特征树，才能改 IFC 墙的厚度参数。代价比「只读导入三角网」大。
3. **语义导入要过 IfcParse。** `IShapeOps` 现在只返回 `MeshCpu`，扛不住 IFC 语义，也扛不住特征树。需要一个 IFC 专用导入器，产出 `(语义场景图 + 特征树/网格 + 材质)`。

---

## 7. 里程碑（M6 已完成，主线 = 参数化内核）

### 主线：参数化编辑内核（P1–P4）

| 里程碑 | 内容 | 目标 |
|---|---|---|
| **P1** | 特征树 + 求值器，只支持 2-3 个操作（Box + Extrude） | 跑通「改参数 → 重算 → 渲染」闭环 |
| **P2** | 加 Fillet / Boolean / Chamfer + 命令化 undo | 有真实建模能力 |
| **P3** | 拓扑命名（先索引法，再几何法） | 改上游参数不炸下游 |
| **P4** | 特征树序列化进 `.tdoc` + 建模 UI | 完整编辑闭环 |

### 支撑线（与主线并行）

| 里程碑 | 内容 |
|---|---|
| **材质/纹理** | RHI 真 texture/UBO/sampler，双后端，glTF PBR 基础（编辑预览也需要它） |
| **IFC 导入** | IfcOpenShell 接入 → 语义场景图 + 特征树/网格 + 材质映射（对齐 P 线） |
| **大模型渲染** | 合批/instancing + 视锥裁剪 + 渐进加载 |
| **截面/分类着色/选择** | 截面裁剪 + Appearance Profiler + 轮廓/多选 |
| **属性面板/大纲树/测量** | 编辑的交互配套 |
| **导出 IFC** | 编辑后交付，**现在成了刚需**（原 M12「可选」改为「必做」） |

**下一步 = P1**：它是试金石——在自己代码里让「改盒子长度 → 特征树重算 → 新三角网 → 屏幕更新」跑通，后面都是在这骨架上加肉。

---

## 8. 建议的第一步（可执行）

1. 设计特征树最小数据模型（`Feature{ id, kind, inputs, params }`）+ 求值器（Box + Extrude 两个节点够用），挂在现有 OCCT 集成上。
2. 让 `Shape`（[shape_ops.h](../src/modeling/shape_ops.h)）从「read_file + tessellate」升级为「持有特征树 + evaluate() → TopoDS_Shape + tessellate()」。
3. 做一个极简入口：改参数 → 重算 → 上传新三角网 → 渲染更新，先跑通闭环，再谈 UI 和拓扑命名。

并行：确认 IfcOpenShell 与 OCCT 版本匹配，跑通最小 demo（解析一个 IFC → 打印空间结构树）。

---

## 9. 已决定 & 待拍板

**已决定：**

- **编辑目标 = 参数化编辑**（FreeCAD 方向），不是纯查看、也不是直接编辑。→ 决定了「特征树第一等公民」的架构。
- **IFC 写回 = 必做**（编辑后要交付），不再可选。
- **单 app + 共享内核，MCAD/BIM 不拆两个软件**：域差异用工作台分层，MCAD 编辑做深、BIM 编辑做浅（只编辑 IFC 参数，不做完整 BIM 建模）。详见 [DECISION-MCAD-BIM.md](DECISION-MCAD-BIM.md)。
- **渲染场景图 = VSG 式（节点 + 访问者 + 命令图状态）**：渲染侧场景图用「节点 + 访问者」组织、渲染状态用命令图（`StateGroup`/`StateCommands`）挂载；它是语义树的「绘制投影」而非复制，语义树仍是层级唯一真相源，靠脏标记增量同步。详见 [SCENE-GRAPH.md](SCENE-GRAPH.md)。

**待拍板：**

- **拓扑命名策略**：P3 先上索引法（快、脆）还是直接啃几何法（稳、难）？建议索引法先行。
- **IFC 几何导入粒度**：导入成「特征树」还是「三角网 + 可选特征树」？前者能编辑 IFC 参数但代价大，后者先只读、后补编辑。
- **特征树与场景图的关系**：特征树是否就是新的「几何资产」形态（替换 `MeshAsset`），还是与之并行？
