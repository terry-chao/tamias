# 视锥剔除方案

> **一期已落地**（叶子 AABB × 六平面）。二期语义树剪枝、三期 BVH 视锥还没做。渲染基线见 [渲染管线](RENDERING.md)；视锥怎么变成 NDC 立方体见 [视锥、NDC 与屏幕](NDC.md)；语义树 / `world_bounds` / 展平见 [场景图](SCENE-GRAPH.md)。

目标只有一件事：**镜头看不见的构件，CPU 不要再发 `draw_indexed`。** GPU 顶点裁剪仍会丢掉屏外三角，但 draw 的开销已经付过了。一万个构件全在屏外，现在仍是一万次提交。

不引入渲染场景图，也不做遮挡剔除（Hi-Z / 遮挡查询放到合批之后）。

---

## 0. 贴着现有代码走

| 已有 | 没有 |
|---|---|
| 节点 `local_bounds` / `world_bounds`（自身 + 子树） | `SceneDrawItem` 不带包围盒 |
| `perspective * look_at`（CPU 按 OpenGL 习惯，Y-up） | 合批 / instancing |
| 拾取 BVH（世界盒，只给点选用） | 二期语义树剪枝、三期 BVH 视锥 |
| **一期：展平时丢掉屏外叶子** | 遮挡剔除 |
| `draw_channel` 对每个可见 item 一次 draw | |

当前展平：`Document::render_items(const Frustum*)` 扫一遍语义树，有网格且（无 frustum、或世界包围盒未完全在视锥外）才塞进 `SceneDrawItem`。视口每帧传入 `proj * view` 抽出的六张平面。不传 frustum 时仍是全量清单（测试、离屏）。

Vulkan 的 `clip_space_correction_matrix()` **只在 GPU 上乘**，翻的是 NDC 的 Y/Z。世界空间里的视锥金字塔没变。**提平面用 `proj * view`，不要乘 clip 校正。**

网格、天空、坐标轴、预览线不走剔除，继续每帧画。

---

## 1. 原理（保守：允许多画，禁止漏画）

相机把世界切成一个四棱台：左、右、上、下、近、远 **6 张平面**。物体的世界 AABB 若**整盒**在某一张平面的外侧，这一帧不画。

- 盒和棱台相交、但三角其实在屏外 → 仍会画（多画，安全）。
- 盒判在外面、三角其实在屏上 → **禁止**。所以只用 AABB，并给平面留一点 epsilon。

测试（每个平面一次）：取 AABB 上沿**内向法线最靠里**的那个角（p-vertex）。该角已在外侧 ⇒ 整盒在外 ⇒ 剔除。只要有一个角还在内侧（盒和平面相交）⇒ 保留。盒子无效（`!valid()`）⇒ 保留，避免漏画。

---

## 2. 分三期：同一句「整盒在外则跳过」，走不同的结构

剪枝动作相同：父盒子整个在镜头外，下面一枝全扔掉。差别是那棵树按什么长出来。

```
一期  每个有网格的叶子 × 六平面     ← 已落地
二期  顺着语义树（楼层 / 建筑）剪枝
三期  扁平大场景才用拾取 BVH 空间剪枝
```

GPU 遮挡、Hi-Z、间接绘制放到合批之后，不在这三期里。

---

## 3. 一期：展平时丢掉屏外叶子 ✅

场景扁平时（墙全挂在根上）已经能砍掉绝大部分 draw。已实现、有单测。

### 做什么

在现有那次扫描里加一层筛选：有网格、并且世界包围盒没完全落在镜头外，才塞进 `SceneDrawItem`。

```
现在：  有网格 → 全部进清单 → 全部 draw
一期：  有网格 → 盒在视锥外？丢掉 : 进清单 → 只画剩下的
```

不改语义树结构，不建 BVH，不改 GPU 提交方式。

### 落地改动（约三处）

1. **从 `proj * view` 抽出六张平面**（不要乘 clip 校正）。
2. **AABB vs 平面**：p-vertex 测试 + epsilon。
3. **展平可选传入视锥**：

```
视口：Frustum f = Frustum::from_view_proj(proj * view);
      frame.items = document_->render_items(&f);

render_items：
  有网格？
    无 frustum 或盒无效 → 照旧加入
    盒完全在视锥外 → continue
    否则加入
```

无 frustum 的调用保持全量清单（测试、离屏、还不关心相机的路径）。

### 单测

不需要起 GPU。假相机即可：盒子在视锥内 → 清单有它；平移到侧面或近裁前面外 → 清单没有。贴边、无效盒 → 必须仍在清单里。

---

## 4. 二期：顺着语义树剪枝

`SceneNode.world_bounds` 已经是「自己 + 子树」。DFS：分组节点整盒在外 → 子树全部不提交。

```
楼
 ├─ 1F          ← world_bounds = 这一层所有墙的并盒
 │    ├─ 墙A
 │    └─ 墙B
 └─ 2F
      └─ …
```

镜头只对着 1F 时，2F 的大盒子在画面外 → 这一层的墙一次都不测。楼层 / 建筑这种层级一旦建起来，一期几乎零成本变成二期：还是那句「整盒在外则跳过」，只是从叶子走到了分组节点。

**前提**：场景里真有分组。墙全是根节点时，这棵树是扁的，二期几乎等于一期。

---

## 5. 三期：用拾取 BVH 做空间剪枝

仅当叶子极多且树很扁（和语义树无关的 10 万散件）。BVH 比「每个叶子测六平面」更能提前丢掉整簇。**先复用现成 `Bvh`，不要为剔除另做一套加速结构。**

### 和二期不是同一棵树

| | 二期语义树 | 三期 BVH |
|---|---|---|
| 树从哪来 | `parent` / `children`（楼层、建筑） | 按盒子中心拆半（已有拾取树） |
| 问什么 | 这层楼在不在画面里 | 空间上这一坨在不在画面里 |
| 树很深、分组合理 | 几乎够用 | 多余 |
| 十万散件、全是根 | 没东西可剪 | 才需要 |

BVH **不是**场景图。它依赖的是「一堆带世界包围盒的可画物体」，不是 `parent / children`。数据从 `SceneNode` 读，算法是空间索引。

### 构建与遍历

不是语义树里每一个节点。现成 [picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp) 的 `Bvh::build` 只收「有网格、且 `world_bounds` 有效」的叶子。纯分组不进 BVH。

```
语义树（读叶子的 world_bounds）
    → 按盒子中心左右拆半，建成 BVH
    → 从根往下走：
         大盒子整个在镜头外 → 这一枝全扔
         和大盒子相交 → 再看左右孩子
         走到叶子还在视锥里 → 才进 draw 清单
```

点选已经是「射线打盒子」；视锥剔除只要换成「视锥套盒子」。有楼层时二期就够，不必上 BVH。

---

## 6. 不要和这些搞混

- **语义树剪枝 ≠ BVH。** 前者问归属，后者问空间远近。
- **不做渲染场景图。** 语义树留在 `Scene` / Document；渲染仍只拿平铺的 `SceneDrawItem`。BVH 是语义树之外的加速结构，和 [场景图](SCENE-GRAPH.md) 里「空间索引不是把语义树搬进渲染」一致。
- **不是遮挡剔除。** 墙在楼后面、盒仍与视锥相交 → 一期到三期都会画。遮挡更贵，放到合批之后。
- **小场景可以暂时不做。** 几十上百个物体时，全画可能比提平面还省。BIM 规模（一层几千面墙、整栋十万构件）才是刚需。

---

## 附录：源码锚点

| 文件 | 角色 |
|---|---|
| [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) `render_items()` | 一期筛选落点 |
| [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) `world_bounds` | 叶子 / 子树并盒（已有） |
| [scene.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.cpp) `recompute_world()` | 自底向上算盒 |
| [math.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/math.h) `Frustum` / `Aabb` / `perspective` / `look_at` | 六平面 + 盒子 vs 平面 |
| [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp) `submit_current_frame()` | 传入 `proj * view` |
| [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) / [picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp) | 三期复用的 BVH |
| [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) `draw_channel` | 消费清单；剔除发生在提交之前 |
