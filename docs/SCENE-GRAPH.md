# 语义树

> OCCT 的场景图实现到哪、Tamias 的语义树和它什么关系、展平怎么接到渲染。结论都用当前代码验证过。三角网如何画到像素见 [渲染管线](RENDERING.md)。
>
> 站点导航里「场景图」是这一层的**总称**（语义树 / 展平 / 将来的渲染场景图）。本文单独说「场景图」时，仍特指渲染侧那棵显示树。分层入口见 [场景图](scene/index.md)。

---

## 0. 结论先行（TL;DR）

| 问题 | 结论 |
|---|---|
| 「场景图」是什么？ | 一个**渲染侧**的概念：用父子树组织「要画的东西」，实现变换继承、组织管理、层级剔除。**它不是语义概念。** |
| OCCT 的场景图是什么？ | 一个「够自己显示用」的**最小留存式场景图**（`Graphic3d_Structure`），只服务「显示 + 拾取」，语义/空间剔除/实例化它全不做。 |
| Tamias 有没有场景图？ | **没有。** 你只有「BIM 业务层 + 语义树（`Scene`）+ 展平渲染（`render_items()`）」。建筑规则在 BIM 层，树在 `Scene`，渲染侧只拿平铺的 `SceneDrawItem`。 |
| 它们对应关系？ | OCCT 场景图 ↔ Tamias 的**渲染侧**（`SceneDrawItem` 列表）；Tamias 语义树 ↔ OCCT 的 **OCAF 文档树**（数据层）。两者靠「展平」桥接。 |
| BIM 身份改变了什么？ | 没有推翻上面的架构；只是渲染侧要补的东西变多（实例化、空间剔除、LOD、剖切、按楼层可见性），而这些 **OCCT 场景图一个都没给**——所以自研渲染是对的。 |

---

## 1. 先掰清一个词：「场景图」有三重歧义

讨论中最容易混的地方，是「层」和「树」被反复用于不同维度。先钉死三个词：

| 词 | 属于哪一侧 | 是什么 |
|---|---|---|
| **语义树 / 对象树**（semantic tree） | 数据侧 | 通用父子树（`parent` / 变换）。Tamias 的 `Scene` 就是。建筑含义（楼层、轴网）在 [BIM 业务层](BIM.md)。 |
| **场景图**（scene graph） | 渲染侧 | 表达「要画的东西怎么组织、怎么变换、谁盖谁」的**显示层级**。OCCT 的 `Graphic3d_Structure` 就是。 |
| **展平列表**（flattened draw list） | 渲染侧输入 | 语义树烘掉层级后、只剩「网格 + 全局矩阵」的平铺结果。Tamias 的 `SceneDrawItem` 就是。 |

**场景图从来都是渲染概念**。Blender/Maya 的 Outliner、Unity 的 GameObject 层级、OSG/OpenInventor，都是渲染侧的东西。把 Tamias 的那棵「楼层→墙」树叫「场景图」是术语误用——它叫「语义树」。

而「多层」在 OCCT 身上又分两个不相干的维度（这是之前绕晕的来源）：

- **架构分层**：代码的纵向流水线 `AIS → PrsMgr → Graphic3d → OpenGl`，一层干一种活，只跟相邻层说话；
- **渲染层（z-layer）**：画面的横向叠放「网格在下 / 模型在中 / 标注在上」，谁盖着谁。

两者都叫「层」，但一个是「谁负责哪道工序」，一个是「谁盖着谁」，互不相干。

---

## 2. OCCT 的场景图：实现到什么程度

### 2.1 它真的实现了

OCCT 的显示侧是货真价实的**留存式场景图**，节点是 `Graphic3d_Structure`：

```
Graphic3d_Structure          ← 一个「可显示的东西」
  ├─ 变换 SetTransform()      ← 放在世界哪个位置
  ├─ 外观（颜色/材质/线宽）
  ├─ 显示模式（着色/线框/消隐）
  ├─ 优先级 priority + z-layer ← 决定谁先画、谁盖谁
  ├─ 可见性开关
  ├─ Connect() 父子连接        ← 子继承父的变换/可见性
  └─ 若干 Group → Graphic3d_ArrayOfPrimitives（三角形/线段数组）
```

这些节点由 `Graphic3d_StructureManager` 管，挂到 `V3d_Viewer` 上显示。它是**留存式**的：三角形一旦算好就常驻内存，重绘不重算，只遍历已存结构提交 GPU。

### 2.2 它覆盖了「正常场景图」的哪些用途

| 场景图通用用途 | OCCT 做到没有 |
|---|---|
| 组织管理 | ✅ 但只是**显示侧**的组织，不碰语义 |
| 变换继承 | ⚠️ 有 `Connect()`，但日常几乎不用，深度≈1 |
| 层级剔除 | ⚠️ 只有单结构基本剔除，没有 BVH/八叉树 |

### 2.3 它**刻意不实现**的

| 正常场景图会做 | OCCT 做不做 |
|---|---|
| 深度变换继承（机械臂式多级联动） | ❌ `Connect` 有但浅 |
| 空间加速剔除树（BVH/八叉树） | ❌ |
| 实例化 / 合批（海量同几何） | ❌ 一个 structure 一次 draw，10 万构件扛不动 |
| 语义组织（Outliner / 命名 / 分组） | ❌ 那是 OCAF 文档树的活 |
| 动画 / 骨骼 / 组件挂载 | ❌ 完全没有 |

### 2.4 为什么它是这个形状

OCCT 是**几何内核 + 显示 SDK**，它的可视化模块使命只有一条：**「把形状显示出来，并让你能选中它。」** 所以它的场景图只做到「能驱动显示、能管顺序、能留存、能拾取」为止。语义给 OCAF，空间/实例化它知道自己扛不动 10 万构件，压根不设计。

一句话：**OCCT 实现场景图，不是为了给你搭世界，而是为了把自己的「显示」跑起来——它是「自用」的，不是「通用」的。**

---

## 3. Tamias 的「树」：语义树 + 展平渲染

Tamias **没有场景图**，它有两样东西，分工清晰。

### 3.1 语义树：`Scene` / `SceneNode`

[scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) 里：

```cpp
struct SceneNode {
  std::uint64_t id = 0;
  std::uint64_t parent = 0;            // 0 = 根；parent 是唯一真相源
  std::vector<std::uint64_t> children; // 派生缓存，由 recompute_world() 重建
  std::uint64_t mesh_asset_id = 0;     // 0 = 分组节点（无几何）
  Mat4 local_transform = Mat4::identity();   // 父空间局部变换
  Mat4 world_transform = Mat4::identity();   // 缓存：父链累积出的全局变换
  Aabb local_bounds{};                        // 自身几何在局部空间
  Aabb world_bounds{};                        // 缓存：自身+子树在全局空间
  bool selected = false;
};
```

这就是你的 **语义树**——通用的「谁是谁的孩子 + 变换继承」。它**不碰任何 GPU/渲染资源**（注释明说：几何只引用 `mesh_asset_id`，渲染资源在 render 侧）。

「墙属于楼层、窗属于墙」这类**建筑规则不在这一层解释**。`parent` 只是链接；谁该写成楼层、当前标高是哪一个，由 [BIM 业务层](BIM.md) 决定后调用 `set_parent`。现状没有楼层系统，新建墙 `parent = 0`（挂在根上）。将来的「建筑→楼层→族→实例」是 BIM 层写出来的树形，不是 `SceneNode` 上的新字段。

### 3.2 局部→全局：`recompute_world()`

[scene.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.cpp) 的 `Scene::recompute_world()` 做的正是我们讨论的「变换继承」数学：

```cpp
n->world_transform = parent_world * n->local_transform;  // 自顶向下累积
```

- **自顶向下**算全局变换：`世界 = 父世界 × 自身局部`；
- **自底向上**算全局包围盒：子包围盒并入父；
- 有防环保护（`set_parent` 拒绝把节点设成自己的后代）。

这正是「墙的全局坐标 = 楼层变换 × 墙局部变换」的落地。**变换继承由语义树承担**；**楼层这个身份由 BIM 业务层承担**。语义树不知道「父节点是楼层」，只知道「有一个父、世界矩阵要乘上去」。

### 3.3 展平渲染：`render_items()` → `SceneDrawItem`

[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) 的 `Document::render_items()`：

```cpp
for (const auto& node : scene_.nodes()) {
  if (node.mesh_asset_id == 0) continue;   // 分组节点不产出几何
  if (frustum && !frustum->intersects(node.world_bounds)) continue;
  SceneDrawItem item{};
  item.transform = node.world_transform;    // ← 已经烘好的全局矩阵
  item.selected  = node.selected;           // ← 选中态也带下去了
  // 解析 material_id → base_color / roughness / metallic / 纹理 id
  items.push_back(item);
}
```

[render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h) 里的 `SceneDrawItem`：

```cpp
struct SceneDrawItem {
  std::uint64_t mesh_asset_id = 0;
  Mat4 transform = Mat4::identity();   // world transform（已烘好）
  Vec3 color{...}; float roughness; float metallic;   // PBR
  std::uint64_t albedo_texture_id, normal_texture_id;
  bool selected = false;
};
```

**关键**：`render_items()` 输出的是**平铺列表**——遍历语义树、跳过纯分组节点和（可选）屏外叶子、把每个有几何的节点烘成一个「网格 + 全局矩阵 + 材质 + 选中态」的叶子。渲染侧拿到它时**已经不知道「墙的爸爸是楼层」**，只知道「这个网格要放在这个世界位置」。

### 3.4 渲染侧：留存 GPU 网格 + 每帧展平提交（半留存）

[render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h) 里，`RenderThread` 持有 `meshes_`（`upload_mesh` 幂等缓存，几何**留存**），而 `FrameSubmission` 每帧带一整份 `SceneDrawItem` 列表（场景结构**非留存**）。

所以 Tamias 是「**半留存**」：

| | Tamias | OCCT |
|---|---|---|
| 几何 GPU 资源 | ✅ 留存（`meshes_` 缓存） | ✅ 留存（VBO） |
| 场景结构 | ❌ 每帧展平重发 | ✅ `Graphic3d_Structure` 常驻 |
| 排序 / layer | 隐式（items 顺序 + 单独 pipeline） | 显式 z-layer + priority |
| 拾取 / 高亮 | 只有 `selected` 字段，渲染侧未消费 | 留存结构复用 + 离屏渲染 |

## 4. 实现地图：场景图不止一个文件

常被误以为「场景图」只写在 `scene_graph.cpp`。实际上那个文件只是**渲染侧核心**（节点树 + 命令 + 访问者）；整套场景图是横跨语义侧、展平桥、传输协议、渲染线程的系统——`scene_graph.cpp` 是引擎的心脏，但血管从语义树一直通到 GPU 命令，缺一环都跑不起来。这一节是代码走读地图。

### 4.1 总数据流

```
Scene（语义树，数据侧唯一真相源）
  │  mutator 变更 → generation++ + 按代次盖章脏节点
  ▼
Document::render_items()（展平：烘全局矩阵，产出 SceneDrawItem）
  │  FrameSubmission 带上 scene_generation / scene_dirty_ids / hidden_node_ids
  ▼
RenderThread::draw_channel()（渲染线程）
  │  代次没变 → 跳过更新；变了 → build_scene_graph / update_scene_graph
  ▼
留存场景图（按 channel 持有）→ RecordCommands 访问者录制 GPU 命令
```

### 4.2 各文件职责

| 文件 | 角色 | 关键内容 |
|---|---|---|
| [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) / [scene.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.cpp) | 语义树 + 脏标记源头 | `generation()` / `dirty_since(last)` / `mark_dirty` / `mark_subtree_dirty`；所有 mutator 挂钩，变换/换父标记整棵子树 |
| [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) `render_items()` | 展平桥 | 语义节点 → `SceneDrawItem`（网格 + 世界矩阵 + 世界包围盒 + 解析后的材质/选中/线条） |
| [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h) | 共享类型 | `SceneDrawItem`、`GpuMesh`、`GpuTexture`、`PushConstants`（从渲染线程私有区提出，供场景图共用） |
| [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.h) / [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp) | **渲染侧核心** | 节点（`Group`/`Transform`/`StateGroup`/`Drawable`）、`StateCommand` 命令、`RenderVisitor` / `RecordCommands` 访问者、`build_scene_graph`（全量）/ `update_scene_graph`（增量） |
| [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h) / [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) | 传输 + 集成 | `FrameSubmission` 字段；channel 留存树（`scene_root` + 节点索引）；`draw_channel` 增量同步与录制；GPU 网格/纹理缓存（`meshes_` / `textures_`） |
| [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp) / [viewer_host.cpp](https://github.com/terry-chao/tamias/blob/main/src/wasm/viewer_host.cpp) | 消费侧组装帧 | 每帧提交 `scene_generation` + `dirty_since(上次游标)`；视口隐藏过滤（floor/类别/isolate）收集成 `hidden_node_ids` |
| [set_material_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/set_material_command.cpp) | 旁路脏标记 | 材质引用变更不走 `Scene` mutator，命令里显式 `mark_scene_dirty(entity_id)` |
| [scene_graph_tests.cpp](https://github.com/terry-chao/tamias/blob/main/tests/scene_graph_tests.cpp) | 验证 | 脏标记（`SceneDirty`）+ 增量同步（`SceneGraphIncremental`）+ 录制剔除/隐藏的单测 |

### 4.3 渲染侧核心：`scene_graph.h / .cpp` 里有什么

四类节点（`RenderNode` 的派生类，只存数据、不碰 GPU 资源）：

- `GroupNode`：容器，孩子的集合；
- `TransformNode`：带局部矩阵，作用于整棵子树；录制时访问者维护世界矩阵栈做累积；
- `StateGroupNode`：挂 `StateCommand` 列表 + 孩子；命中它时把命令显式录进上下文状态；
- `DrawableNode`：叶子，`mesh_asset_id` + 语义 `node_id` + 世界包围盒；录制时真正发 draw。

配套三样东西：

- `StateCommand` 家族（`BindMaterialCommand` / `SetSelectedCommand` / `SetLinesCommand`）：数据 + `record(ctx)`，命令图式**显式覆盖**——状态沿遍历线性累积，子树要什么状态就在自己的 `StateGroup` 里再下命令，不是 OSG 的隐式继承；
- `RenderVisitor` + `RecordCommands`：访问者模式（节点 `accept(visitor)` → `apply(node)`）。`RecordCommands` 维护矩阵栈、录制状态、解析 GPU 资源并下发 `set_pipeline / set_push_constants / draw_indexed`，同时做视锥剔除与隐藏集判断；
- 两个入口：`build_scene_graph(items)` 全量构建（首帧 / 兜底），`update_scene_graph(root, index, items, dirty_ids)` 增量更新（就地改矩阵/命令，增删子树）。

### 4.4 一帧里实际发生什么（走读）

1. 用户操作（拖动 / 改材质 / 增删构件）→ `Document` API 或命令改语义树 → `Scene` 代次 +1、脏节点按当前代次盖章；
2. 下一帧 `submit_current_frame`：`render_items()` 全量展平（同步源，树要保持完整内容），带上 `scene_generation` 和 `dirty_since(上次)` 提交给渲染线程；
3. `draw_channel`：首帧 `build_scene_graph` 全建；之后代次相同就什么都不重建，代次变了才按脏 id 就地更新/增删（脏列表为空走整树重建兜底）；
4. `RecordCommands` 从留存树录制：推矩阵 → 录命令 → 命中 `Drawable` 时查 GPU 网格、做视锥/隐藏判断、发 draw。

### 4.5 结论

- `scene_graph.cpp` 是「节点树 + 命令 + 访问者」的实现——没有它就没有场景图的形状和遍历规则；
- 但真正让场景图「活起来」的还有：`scene.h/.cpp` 的脏标记（变化从哪来）、`document.cpp` 的展平（数据怎么过桥）、`render_types.h`（共享类型）、`render_runtime.h/.cpp`（留存树住哪、每帧怎么同步和录制）、app 两个文件（谁提交代次和隐藏集）、`set_material_command.cpp`（旁路脏标记），以及 `scene_graph_tests.cpp`（验证）。

一句话：**`scene_graph.cpp` 是心脏，但整条血管系统从语义树一直通到 GPU 命令，缺一环场景图都跑不起来。**

---

## 5. 深度对照：三者怎么对应

```
Tamias 语义树 (Scene)          OCCT 的 OCAF 文档树 (TDocStd_Document + TDF)
    │  数据侧，管「谁属于谁」        │  数据侧
    │  展平：烘全局矩阵              │  桥接：语义变化→更新对应 AIS_Shape
    ▼                              ▼
Tamias 展平列表 (SceneDrawItem)  OCCT 场景图 (Graphic3d_Structure 集合)
    │  渲染侧，管「画在哪、什么色」   │  渲染侧
    ▼                              ▼
Tamias 渲染 (render_runtime)     OCCT 渲染 (V3d_Viewer + OpenGl 驱动)
```

**三条对应关系，两条在渲染侧，一条在数据侧：**

| Tamias | OCCT 对应 | 是不是一回事 |
|---|---|---|
| 语义树（楼层→墙） | OCAF 文档树 | 对应，但这是**数据层**，不是场景图 |
| `SceneDrawItem`（网格+变换+材质） | `Graphic3d_Structure`（图元+变换+外观） | ✅ 对应，都是渲染侧「一个可画物」 |
| 平铺 draw 列表 | `V3d_Viewer` 里的 structure 集合 | ✅ 对应，都是渲染侧叶子集合 |

**语义树 ↔ 场景图之间没有直接对应**，它们靠「展平 / 桥接」连起来。在 OCCT 世界里你是「遍历 OCAF → 算全局变换 → 更新 `AIS_Shape`」，在 Tamias 里是「遍历 `Scene` → 烘全局矩阵 → 生成 `SceneDrawItem`」——**同一件事的两种实现**。

---

## 6. 关键洞察

1. **场景图是渲染概念，语义树是数据概念，别混。** 混了会把「墙属于楼层」这种语义塞进渲染结构，导致两份真相、还拖垮海量构件的遍历。建筑规则也不该写进 `SceneNode` 字段，见 [BIM 业务层](BIM.md)。

2. **OCCT 的场景图是「够显示用的最小实现」。** 它服务自己的「显示 + 拾取」，语义/空间/实例化全不做。所以「用 OCCT 场景图来管 BIM 层级」是错的方向。

3. **展平是 MCAD/BIM 的常态，不是 Tamias 的简化。** 就连 OCCT 自己的场景图，日常用法深度也≈1（一个零件一个 structure，几乎不 `Connect`）。Tamias 的平铺 `SceneDrawItem` 列表在结构上几乎等价，差别只在：OCCT 叶子多了 priority / z-layer / highlight / clipping，且是留存式；Tamias 是每帧展平重发。

4. **BIM 身份强化了自研渲染的合理性。** BIM 要的是实例化、空间剔除、LOD、剖切、按楼层/类别可见性——OCCT 场景图一个都不给。所以「自研渲染 + 展平 + （未来的）空间索引」是对的，不该回头用 OCCT 场景图。

---

## 7. 未来的路（按 BIM 的优先级）

| 要补的 | 说明 | 现状锚点 |
|---|---|---|
| **highlight / selection** | 点中墙、高亮整层，交互刚需 | `SceneDrawItem.selected` 字段已留位，渲染侧未消费 |
| **z-layer 抽象** | 轴网/标高/标注/剖切框这些 overlay，别再布尔硬编码 | `FrameSubmission` 里 `show_axes/show_grid/show_preview_line` + 单独 pipeline 是当前笨办法 |
| **空间索引 + 实例化** | BIM 规模（几万构件）的剔除与合批 | 视锥一期已落地，见 [视锥剔除](FRUSTUM-CULLING.md)；合批 / BVH 视锥仍缺。加速结构不是场景图 |
| **按楼层/类别可见性** | 「只看结构柱」「关掉 MEP」 | [BIM 业务层](BIM.md) 查出 id，渲染侧只收可见性标志 |

**最重要的提醒**：最后两项（空间索引、实例化）补的是**渲染侧加速结构（八叉树/BVH + 实例表）**，不是「把语义树搬进渲染」。语义树永远留在 `Scene` / Document，渲染永远只拿展平结果。

> **已定（记录于 [ROADMAP.md](ROADMAP.md) 第 4/9 节）**：将来实现渲染侧结构时，采用 **VSG 式「节点 + 访问者」+ 命令图状态**（`StateGroup` + `StateCommands`），而非 OSG 的 `StateSet` 隐式继承。注意：这里的「节点」是 draw-oriented（drawable + transform + material + 可见性），**不是语义树的复制**——VSG 的节点树本身就是「数据数组 + 命令」导向，与「展平渲染」同向，不冲突。

> **现状（已落地）**：上述设计已落地为 [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.h) / [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp)（`Group` / `Transform` / `StateGroup` / `Drawable` 节点 + `StateCommand` 命令 + `RecordCommands` 访问者），`draw_channel` 的模型循环已切换到访问者录制。**增量同步已接**：`Scene` 每个 mutator 递增 `generation` 并按代次盖章脏节点（变换/换父标记整棵子树，选中/材质只标记节点）；app 每帧把 `scene_generation` + `dirty_since(last)` 随 `FrameSubmission` 发给渲染线程，渲染线程按 channel 留存场景图，只在代次变化时 `update_scene_graph` 就地更新/增删对应子树；代次变了但无脏 id（clear / 整档恢复）走整树重建兜底。视锥剔除与可见性过滤移到录制时按节点判断，树本身保持完整内容。状态语义是命令图式的显式覆盖（状态沿遍历线性累积，子树要覆盖就在自己的 `StateGroup` 里再下命令），不是 OSG 的隐式继承。

---

## 附录：涉及文件

- [BIM.md](BIM.md) —— 楼层 / 轴网 / 宿主；语义树只记账
- [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) / [scene.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.cpp) —— 语义树 + 局部→全局变换累积
- [scene_graph.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.h) / [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp) —— 渲染场景图核心（VSG 式节点 + 命令 + 访问者；全量构建 + 脏标记增量更新）
- [document.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.h) / [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) —— 展平 `render_items()`
- [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h) / [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) —— 留存树 + `draw_channel` 增量同步与录制
- [scene_graph_tests.cpp](https://github.com/terry-chao/tamias/blob/main/tests/scene_graph_tests.cpp) —— 脏标记与增量同步的单测
- [视锥剔除](FRUSTUM-CULLING.md) —— 展平时丢掉屏外叶子；二期语义树剪枝；三期复用拾取 BVH
- [render_types.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h) —— 展平结果 `SceneDrawItem`
- [render_runtime.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.h) —— 半留存渲染侧
- [occt_shape_ops.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_shape_ops.cpp) —— BRep → 三角网（渲染侧数据来源）
