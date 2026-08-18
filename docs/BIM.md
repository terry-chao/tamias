# BIM 业务层

> 墙、梁、板、柱、门、窗、轴网、楼层这些**建筑语义**不该散落在 Qt、命令或语义树里。它们有自己的一层：介于「命令」和「Document / Scene」之间。几何怎么挤、节点怎么挂父子，分别是造型层和语义树的事；**谁属于哪一层、贴哪道轴、窗开在哪面墙上**，是这一层的事。

**现状：** 没有楼层系统，也没有轴网。墙梁板柱已经能画。**门窗宿主关联已经落地**：点在墙上放窗/门会写入 `HostedOn` 关系，墙一改就通知开口重造型。本文是这一层的落点。

代码在 [`src/bim/`](https://github.com/terry-chao/tamias/tree/main/src/bim)。实体几何仍在 [`src/entity/`](https://github.com/terry-chao/tamias/tree/main/src/entity)，命令仍在 [`src/command/`](https://github.com/terry-chao/tamias/tree/main/src/command)。`BimModel` 作为 `Document` 的一个侧面挂上（和 `Scene`、实体表并列）。

---

## 1. 为什么要单独一层

现在建一面墙：`CreateWallCommand` 直接 `Document::add_entity()`。`SceneNode.parent` 默认 `0`（根）。语义树**能**表达「墙在楼层下」，但**没有人规定**该挂到哪。

若把「当前标高、轴网捕捉、窗宿主」塞进这些地方，会各自错：

| 若写在 | 错在哪 |
|---|---|
| `Scene` / `SceneNode` | 语义树变成 BIM 内核。盒子、STEP 零件也被逼理解「楼层」 |
| `Document::add_entity` | 通用入树口开始猜建筑规则 |
| `src/app` | Qt 里长业务，无法单测、也无法给 IFC 导入复用 |
| `CreateWallCommand` 里 if-else | 墙、梁、板、窗各写一套归属，规则发散 |

BIM 业务层就是 [MCAD 与 BIM](DECISION-MCAD-BIM.md) 里说的那层**域分叉**：内核（特征树、Scene、渲染、undo）共享；建筑规则集中在这里。MCAD 的盒子/圆柱继续走 `Document`，不经过本层。

这与「BIM 编辑做中浅」不冲突。中浅指的是**不做 Revit 级连接、约束、全专业建模**；楼层、轴网、宿主这些空间结构，连「编辑 IFC 参数 / 导入导出」都要用，必须有落点。

---

## 2. 它在哪一层

插在客户端/命令和场景图之间：

```
┌────────────── Qt 客户端 (app) ──────────────┐
│  窗口 / 视口 / 工具按钮 / 属性面板            │
├────────────── 命令 (command) ───────────────┤
│  事务、撤销；BIM 命令只调本层，不直接 set_parent │
├────────────── BIM 业务层 (bim) ★ ───────────┤
│  关联关系 · 宿主更新 ·（将来）楼层 / 轴网 / 当前标高 │
├────────────── 场景图 (document/scene) ────────┤
│  parent / 变换 / 包围盒（域无关容器）          │
├────────────── 造型 (modeling) ───────────────┤
│  特征树 · 求值器 · 几何边界 (IShapeOps)        │
├────────────── 渲染 (engine/render) ───────────┤
```

和上下的契约：

| 方向 | 本层做什么 | 不做什么 |
|---|---|---|
| 对上（command / app） | `bind_opening_to_host` / `notify_entity_changed`；将来 `place_wall` / `create_storey` | 不碰 Qt、不自己 push undo |
| 对下（Document / Scene） | `add_entity`、写 `BimModel` 关系表、改开口 `local_transform` | 不改 `recompute_world`、不解释网格 |
| 对造型 | 开口需要跟上宿主时调 `Entity::createGeom` | 不写特征树结构、不调 OCCT |
| 对渲染 | 轴网/标高需要 overlay 时只出数据 | 不提交 GPU、不改 draw list |

`SceneNode.parent` 仍是**唯一的树真相**。关联关系是**另一张图**（宿主图），不替代父子。渲染展平后仍然不知道「这是 2 楼」——按楼层显隐由本层查出一串 id，再变成可见性标志交给渲染。详见 [语义树](SCENE-GRAPH.md)。

---

## 3. 管什么、不管什么

**管（建筑语义）：**

| 对象 | 含义 | 在树上的落法 |
|---|---|---|
| 空间结构 | 项目 → 场地 → 建筑 → 楼层（对齐 IFC `IfcBuildingStorey` 那一截） | 楼层 = **分组节点**（`mesh_asset_id = 0`）；**尚未实现** |
| 当前标高 | 会话/文档上的「正在画哪一层」 | 不是 Scene 字段；本层持有 `active_storey_id`；**尚未实现** |
| 墙梁板柱门窗 | 放置、归属楼层、后改宿主 | 叶子节点；`parent` 指向楼层（或门窗指向宿主墙） |
| 轴网 | 定位参考，不是实体构件 | 数据在本层；显示走 overlay；**尚未实现** |
| **关联 / 宿主** | 窗属于墙、门属于墙 | **显式 `Relation`**，存进 `.tdoc` 的 `RELA` chunk；不靠 Z 坐标反推 |

**不管：**

- 墙怎么挤成实体 → `WallEntity` + 特征树（[特征树求值器](FEATURE-TREE-EVALUATOR.md)）
- 世界矩阵怎么累加 → `Scene::recompute_world()`
- 撤销栈 → `Command`
- 盒子 / 圆柱 / 倒角 / 布尔 → MCAD，直达 `Document`

实体类型（`FamilyEntity` / `WallEntity`）继续表示「这是一面参数化墙」。本层表示「这面墙在项目里的位置与关系」。两者不要合成一个类。

---

## 4. 关联关系（已实现）

墙和窗是关联的：墙动了（改厚度 / 长度 / 高度），通过关系找到对应的窗，通知窗重新造型；造型之后对齐，再做合法性检查，然后结束。

### 4.1 数据

```
Relation
  id          关系自己的句柄（.tdoc 里存）
  kind        目前只有 HostedOn
  from        从属构件（窗、门）
  to          宿主构件（墙）
  placement   along / sill / offset（开口在墙上的参数化位置）
  valid       对齐之后是否仍完全落在墙内
```

`BimModel` 持有这张表，作为 `Document` 的侧面。删实体时 `remove_involving` 清掉相关关系。

参数化位置（墙局部）：

| 字段 | 含义 |
|---|---|
| `along` | 沿墙长，0 = 起点，1 = 终点 |
| `sill` | 距墙底的高度（门固定为 0） |
| `offset` | 沿墙厚，0 = 墙中心 |

墙的局部坐标：X = 厚，Y = 高，Z = 长（与 `WallEntity` 特征树经 Z-up→Y-up 后一致）。开口再绕 Y 转 −90°，让窗宽贴墙长。

### 4.2 更新管线

```
墙参数改了（SetFeatureParamCommand）
  → notify_entity_changed(wall)
      → 按 to == wall 查出 HostedOn 的窗/门
      → 通知每个开口：
          1. 重新造型：开口厚度跟上墙厚，createGeom 重算网格
          2. 对齐：along / sill 夹进墙的可用范围
          3. 合法性检查：开口是否仍完全落在墙长、墙高内
      → 结束（不在这里做墙连接裁剪、开洞布尔）
```

代码：[`host_update.cpp`](https://github.com/terry-chao/tamias/blob/main/src/bim/host_update.cpp)、[`host_geometry.cpp`](https://github.com/terry-chao/tamias/blob/main/src/bim/host_geometry.cpp)。

放置窗/门时：视口射线点中墙 → `bind_opening_to_host` → 写入关系并立刻走同一套管线。点在空地上则不建关系，开口保持独立放置。

### 4.3 进 .tdoc

`.tdoc` 格式版本 **6**。新增 chunk `RELA`：

```
next_relation_id : u64
count            : u64
[ id, kind, from, to, along, sill, offset, valid ] × count
```

版本 5 的文件仍能打开（没有 `RELA` 就是空表）。内存快照（undo 用的 `serialize_document`）同样带上关系表。

---

## 5. 没指定楼层时怎么办

**现在：** 没有楼层对象。`add_entity` 不写 `parent`，墙挂在根上。这是合法的「未归属」，不是 bug。宿主关联不依赖楼层。

**楼层落地后：**

1. 用户没点选楼层 ≠ 语义树去猜。归属只来自本层写入的 `parent`。
2. 有当前标高 → `place_wall` 把墙的 `parent` 设成该楼层的分组节点。
3. 没有当前标高、文档里也还没有楼层 → 仍 `parent = 0`，未归属。以后 `set_parent` 即可，不必重算几何。
4. **不要**用墙的 Z 去套楼层标高作为唯一真相（跨层墙、错层、尚未建层都会套错）。Z 最多当辅助建议，写入仍是显式宿主。

```
无楼层（现在）          有当前标高（本层落地后）

Scene 根                Building
 ├─ Wall A               └─ Storey 1F    ← active
 ├─ Window (HostedOn A)      ├─ Wall A
 ├─ Wall B                   └─ Wall B
 └─ Box                     Storey 2F
                             └─ …
```

---

## 6. 和现有代码的接缝

今天墙的入树口（[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp)）不碰 BIM：

```cpp
SceneNode node{};
node.name = entity->name;
node.mesh_asset_id = entity->mesh_asset_id;
node.local_transform = entity->local_transform;
// parent 保持 0
```

落地时不要改成 `add_entity` 内部猜楼层。通用口保持愚蠢；聪明放在本层：

```
CreateWallCommand
  → Document::add_entity(...)          // 仍通用；楼层以后由 BimModel::place_wall 写 parent

CreatePrimitiveCommand（窗/门）
  → Document::add_entity(...)
  → bind_opening_to_host(window, wall, hit)   // 点中墙时
```

盒子命令继续 `Document::add_entity`，不进 `BimModel`。

类型切分（一类一文件）：

```
src/bim/relation_kind.h     // RelationKind
src/bim/host_placement.h    // HostPlacement
src/bim/relation.h          // Relation
src/bim/bim_model.h         // 关系表；将来楼层/轴网也挂这里
src/bim/host_geometry.h     // 墙框、开口尺寸、对齐、合法性
src/bim/host_update.h       // notify / bind
src/bim/storey.h            // 尚未实现
src/bim/grid.h              // 尚未实现
```

`BimModel` 作为 `Document` 的一个侧面挂上（和 `Scene`、实体表并列），这样撤销、`.tdoc` 序列化仍是一份文档。不要做成第二份平行文档。

---

## 7. 深度边界（中浅）

本层**要做**的上限：

- 建楼层、切当前标高、构件挂到层
- 轴网（定位，先正交即可）
- **门窗宿主在墙上**（关联 + 随墙重造型 + 对齐/合法性；开洞布尔可以后做）
- 按楼层 / 类别查询，供大纲树和可见性
- 将来 IFC 进出时，空间结构与本层互译（仍走 [IShapeOps](ISHAPE-OPS.md)，不为 IFC 另开几何通道）

本层**先不做**：墙连接自动裁剪、核心层构造、房间边界生成、全专业 MEP、Revit 式约束系统。那些会把这一层做成第二个 Revit。删墙时开口不会级联删除，只清掉关系，开口留在原地。

---

## 附录：涉及文件（现状）

- [wall_entity.cpp](https://github.com/terry-chao/tamias/blob/main/src/entity/wall_entity.cpp) 等 —— 构件几何配方，不是业务层
- [create_wall_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/create_wall_command.cpp) —— 今日直写 Document；楼层落地后改调 `BimModel`
- [create_primitive_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/create_primitive_command.cpp) —— 窗/门点中墙时调 `bind_opening_to_host`
- [set_feature_param_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/set_feature_param_command.cpp) —— 改参后 `notify_entity_changed`
- [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) —— `parent`；不出现 Storey / Grid / Relation 类型
- [handle_inspector.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/handle_inspector.cpp) —— Ctrl+D 句柄检查窗口（app 调试，不是业务层）
- [DECISION-MCAD-BIM.md](DECISION-MCAD-BIM.md) —— 单 app、域用这一层分叉
- [SCENE-GRAPH.md](SCENE-GRAPH.md) —— 语义树只记账，不解释楼层
