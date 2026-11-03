# 空间索引：点选与框选的 BVH / R-tree 取舍

> **现状**：点选已落地（物体级 BVH）；框选是屏幕 AABB 全量遍历；剔除一期是叶子 AABB × 六平面。本文回答「点选为什么用 BVH 不用 R-tree」「`closest_hit` 怎么判『最前』（§2.1）」「框选谁更强」「大型建筑项目规模下怎么选」，是选型结论与演进方向，不是新实现。

相关：[视锥剔除](FRUSTUM-CULLING.md)、[超大规模三角](MASSIVE-GEOMETRY.md)、[语义树](SCENE-GRAPH.md)、[渲染管线](RENDERING.md)。

---

## 0. 结论先行（TL;DR）

| 场景 | 更优 | 关键理由 |
|---|---|---|
| **当前点选**：单射线求最近命中，整树重建 | **BVH** | 射线剪枝天然契合；没有动态维护需求 |
| **当前框选**：静态，鼠标松开时跑一次 | **BVH 略优，差距不大** | 复用同一棵树；R-tree 的动态优势用不上 |
| **频繁增量增删 / 拖动构件** | **R-tree（R\* / STR）或动态 BVH** | 动态维护是 R-tree 主场 |
| **十万级以上大型建筑项目** | **两者都不是单棵扁平树能解决的** | 先按语义分区，再每分区一棵 BVH |

BVH 和 R-tree 都是 AABB 层级。静态批量建树时二者算法同类，差别只在**节点重叠**与**更新方式**；建筑规模下的胜负由**分层架构**决定，不是在这两者之间二选一。

**说明**：仓库里没有 BVH vs R-tree 的实测基准，下面是基于现有代码与负载特征的分析，怎么验证见 §6。

---

## 1. 现状（代码事实）

| 能力 | 实现 | 说明 |
|---|---|---|
| 点选 | `Bvh::closest_hit` | 物体级 BVH；叶子是 `SceneNode`，叶子内部线性扫三角；「最前」= 射线参数 `t` 最小（§2.1） |
| 框选 | `nodes_in_screen_rect` | 全量遍历所有节点 + 8 角投影 + 屏幕 AABB 比较 |
| 框选触发 | `finish_box_select` | 只在鼠标松开时跑一次，拖拽过程只更新覆盖矩形 |
| 重建 | `rebuild_bvh()` | 编辑命令后**整树重建**，`nth_element` 中位数切分 |
| 剔除一期 | `render_items(Frustum*)` | 叶子 AABB × 六平面，已落地 |
| 剔除三期 | 计划复用现成 `Bvh` | 见 [视锥剔除 §5](FRUSTUM-CULLING.md) |

---

## 2. 点选：为什么用 BVH，「最前」又怎么判

1. **查询形态**：单条射线求 `t` 最小的命中。BVH 的中位数切分让兄弟子树在切分轴上不重叠，DFS 里 `t_box > best_t` 就能整枝丢掉。R-tree 也能打射线，但严格最近命中通常要配优先队列做 best-first 遍历。
2. **更新形态**：`rebuild_bvh()` 清空重来，没有增量插入/删除。R-tree 最大的价值是动态维护时的分裂与重插入，这里完全用不上。
3. **盒子形状**：BIM 里大量细长 AABB（墙、板）。R-tree 的节点 MBR 重叠更严重，射线进入一个节点后可能要钻进多个兄弟分支。
4. **实现成本**：`Bvh` 不到百行、零依赖；像样的 R-tree 要节点容量、分裂启发式、重插入，或引入第三方库。
5. **复用**：同一棵树要服务点选、剔除三期、未来的面 / cluster 二级剪枝。只维护一套索引更划算。

### 2.1 命中判定：什么算「最前」

`closest_hit` 的判据只有一条：**射线参数 `t` 最小**。它跟绘制顺序、深度缓冲、`SceneNode` 在数组里的位置都无关 —— 渲染侧怎么排序、怎么合批都不影响拾取结果。

`camera_ray` 在透视下归一化方向、正交下方向本身就是单位向量，所以 `t` 就是沿视线走过的世界距离；`to_local_ray` 只做刚体逆变换（旋转 + 平移，保距），局部空间算出的 `hit_t` 可以直接和世界空间的 `best_t` 比，不需要换算。

遍历是显式栈的深度优先，状态只有三个：栈（存节点下标）、`best`、`best_t`。

```cpp
std::vector<std::int32_t> stack{root_};
std::optional<PickHit> best;
float best_t = std::numeric_limits<float>::max();
while (!stack.empty()) {
  const auto idx = stack.back();
  stack.pop_back();
  const Node& n = nodes_[idx];
  float t_box = 0.f;
  if (!intersect_aabb(ray, n.bounds, t_box) || t_box > best_t) continue;  // ① 盒子拒绝 + 距离剪枝
  if (n.leaf) {
    if (accept && !accept(sn->id)) continue;                              // ② 可见性过滤
    if (intersect_triangle(local_ray, v0, v1, v2, hit_t) && hit_t < best_t) {
      best_t = hit_t;  best = PickHit{sn->id, t / 3, hit_t};              // ③ 更近才覆盖
    }
    continue;
  }
  if (n.left  >= 0) stack.push_back(n.left);                              // ④ 先压左
  if (n.right >= 0) stack.push_back(n.right);                             //    后进先出 → 右子树先访问
}
```

（`sn` / `local_ray` / `v0..v2` 在上面的骨架里省了；完整实现见 `picking.cpp`，线框与草图走 `intersect_segment` 的另一支。）

四个判定点：

| 判定 | 条件 | 作用 |
|---|---|---|
| 盒子拒绝 | `intersect_aabb` 返回 false | 射线擦不到这个盒子，整枝跳过 |
| 距离剪枝 | `t_box > best_t` | 盒子入口已经在已知最优之后 |
| 可见性过滤 | `accept(id)` 为假 | 隐藏 / 隔离 / 别的楼层不参与求交 |
| 更近才覆盖 | `hit_t < best_t` | 只有更近的命中才改写结果 |

**剪枝为什么不会漏掉更近的命中**：`t_box` 是射线**进入**节点包围盒的参数（`intersect_aabb` 的 `tmin`，起点在盒内时夹到 0），子树里任何真实命中的 `t` 都 ≥ 它；所以一旦 `t_box > best_t`，那棵子树里不可能有更近的结果。盒子整体在相机背后时 `tmax < 0 ≤ tmin`，直接判不相交 —— 透视与正交因此共用同一个函数。

**线段命中同量纲**：草图折线 / `line_list` 线框把 `indices` 两两成段，用 `intersect_segment` 判「射线到线段的最近距离 ≤ `kSketchPickRadius`」，回填的 `hit_t` 是射线上的最近点参数，与三角命中的 `t` 同量纲，所以两条路能直接比大小、共用一个 `best_t`。

**并列时谁赢**：比较是严格 `<`，`t` 完全相等时**先遍历到的赢**；而遍历顺序取决于 `nth_element` 的划分（不稳定）与建树时的文档顺序。于是共面重合的一对表面（两块板贴在一起、线正好压在面边上）在 BVH 重建后可能换一个被选中。要确定性就得显式 tie-break（`t` 相等时比 id），当前没有。

**`accept` 在叶子上才求值**：隐藏的节点盒子照样要走完，只是不参与求交。所以隐藏一整层并不降低查询时间（§4.2 第 3 条、§7 的待办）。

**返回的 id 是什么**：`PickHit::node_id` 是 `SceneNode::id`，而 `Entity::id` 就等于 scene node id（`entity.h` 注释写明「= scene node id（对象身份）」），所以 pick 的返回值可以直接当实体 id 用。例外是导入网格：它有 scene node、没有 `Entity`，返回的仍是节点 id，下游 `Document::entity(id)` 拿到的是 `nullptr`。

**不在树里的东西**各有各的路，点击处理里也有先后：文字注记先判（屏幕矩形命中）、构件走 BVH、都没中才看轴网（屏幕像素距离）。`mesh_asset_id == 0` 的纯分组节点永远不进树，也就永远点不中。

---

## 3. 框选：范围查询下谁更强

框选的本质是**范围查询**——屏幕上那个矩形反投影回三维，是一个视锥区域。理论上这更像是 R-tree 的主场，但实际取舍要看数据是否静态。

### 3.1 对比

| | BVH | R-tree |
|---|---|---|
| 批量建树 | 中位数 `O(n log n)`；LBVH 走 Morton 排序可近 `O(n)` 且易并行 | STR 打包一次成型，质量好 |
| 范围查询 | 同一套遍历换叶子测试，可整枝收下 | 原生场景，同样擅长 |
| 兄弟节点重叠 | 空间切分，切分轴上不重叠 | MBR 可重叠，细长盒尤其明显 |
| 增量更新 | 需重建或动态 BVH（rotation / refit） | 强项 |
| 只改变换 | 可原地 refit | 需删 + 重插 |
| 复用 | 点选 / 剔除 / 面级剪枝共用 | 基本只服务自己 |

**静态数据下，打包 BVH 与 STR 打包的 R-tree，性能是同一量级，互有胜负。** 当前框选只在鼠标松开时跑一次，重建成本被命令批次摊薄，R-tree 的动态优势没有用武之地。

### 3.2 R-tree 什么时候在框选上反超

- 实体持续增量增删、或每帧拖动大量构件，整树重建撑不住；
- 主要交互是二维平面上的矩形窗口查询（图纸、场地、装修排布）；
- 需要落盘分页存储。

### 3.3 当前实现的两个问题

`nodes_in_screen_rect` 现在是全量线性遍历，且用「投影后 8 角的屏幕包围盒」做判定。后者是真实几何屏幕足迹的**超集**，于是：

- `crossing` 会有误选（框到了空处也可能选中构件）；
- `window` 可能漏选（物体真实足迹在框内，但它的世界 AABB 投影出框了，就被判掉）。

### 3.4 落地建议

用现有的 `Frustum::from_view_proj`（[math.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/math.h)）从选择矩形抽出视锥，在 BVH 上做范围遍历：

1. `Frustum::intersects(box) == false` → 整枝剪掉（`window` / `crossing` 都适用）；
2. 盒子完全落在四个侧面 + 近面之内 → `window` 模式可整枝收下，不再下探（early accept；近面必须一起判，否则相机背后的点会破坏 `w > 0` 前提）；
3. 其余下探到叶子，再做现有的投影屏幕 AABB 判定。

这比现在每节点投影 8 个角更快，也顺带把剪枝粒度从「每个构件」提到「每一簇」。任意屏幕矩形转视锥的做法：把矩形在 NDC 里的中心与半宽/半高作为 NDC 缩放平移，乘回 `proj * view` 再抽平面。

---

## 4. 大型建筑项目规模下（10⁵ ~ 10⁷ 构件）

这个量级下要看的不是「哪棵树更快」，而是「哪棵树先退化」。

**大型项目的典型特征**：构件多且尺寸异构（钢筋与整块楼板同处一个索引）、空间强聚集（按楼层 / 房间 / 专业分布）、大量重复实例、可见性切换频繁（楼层显示、isolate、剖切）、编辑按命令批次提交、几何可能流式加载。

### 4.1 对比

| | BVH | R-tree |
|---|---|---|
| 批量建树 | `O(n log n)`；LBVH 近 `O(n)`、易并行、可上 GPU | STR 打包质量好，并行化不如 LBVH 常见 |
| 射线最近命中 | 天然契合 | 通常要 best-first 遍历 |
| 矩形 / 视锥查询 | 同一套遍历换叶子测试 | 原生场景 |
| 节点重叠 | 空间切分；但巨型 AABB 会让祖先盒子全重叠 | MBR 重叠更严重，细长墙板下尤其明显 |
| 增量插入 / 删除 | 需重建，或动态 BVH（rotation / refit） | 强项，R\* 成熟 |
| 只改变换 | 可原地 refit | 需删 + 重插 |
| 分页 / 流式 | 两级 BVH（顶层 tile + 按需加载子 BVH）是主流 | 页式设计有传统优势，几何场景里不那么决定性 |
| 复用 | 点选、剔除、面 / cluster 剪枝、遮挡共通 | 基本只服务自己 |

### 4.2 两者共同的真正瓶颈

1. **巨型 AABB。** 一片贯穿整层的楼板、一道幕墙，盒子覆盖全场景，会让祖先节点互相重叠，查询在那一带退化成近似线性。这不是 BVH 独有，R-tree 一样甚至更糟。缓解手段相同：把大构件按面 / cluster 拆成子层次，或走两级结构。
2. **尺寸异构。** 中位数切分对混合尺度数据不友好——巨型构件的中心只是一个点。大规模下应考虑 SAH 切分或 LBVH。
3. **可见性过滤的位置。** 现在 `accept` 谓词只在叶子求值，隐藏一层楼，遍历仍要走完那层楼的所有节点盒子才发现全被否掉。过滤必须发生在遍历之前。
4. **变换假设。** `to_local_ray` 依赖「场景变换都是刚体（无缩放）」这一前提。大项目里出现缩放、镜像、非均匀变换时该前提失效，这跟选哪种树无关，但会先咬人。
5. **流式加载。** 几何不在内存时，索引要能按需展开子树。

### 4.3 单棵扁平树怎么选

如果确实要一棵扁平树打百万构件：**静态多写少 → SAH BVH 或 LBVH**（建树快、可并行、射线与视锥共用、支持 refit）；**增量编辑为主、窗口查询为主 → STR 批量装载的 R\* tree**。

---

## 5. 大规模推荐架构：三级分层

十万构件以上，正确的做法是分层，而不是在扁平结构里二选一：

```
语义分区（楼层 / 房间 / 专业）
      ↓  只对可见分区遍历
分区内实例 BVH
      ↓  单个巨大 mesh 内部
面 / cluster BVH（结构已留，见超大规模三角 §6）
```

这样点选和框选都是「先跳过整层，再在单棵树里剪枝」：隐藏楼层、isolate、剖切的收益是数量级的，而这不是 R-tree 或 BVH 任何一个能单独提供的。

R-tree 在这个体系里的合理位置，是替代**某个分区**的叶子索引——尤其是二维性强、编辑频繁的子域，比如场地平面、图纸、装修排布。

---

## 6. 怎么验证（基准矩阵）

别靠理论定夺，用真实大项目跑：

| 维度 | 取值 |
|---|---|
| 规模 | 10 万 / 100 万构件 |
| 指标 | 静态建树时间、单次射线耗时、框选耗时、可见性切换后单次查询耗时、移动 N 个构件后的重建开销 |
| 关键参数 | 细长构件比例、巨型构件比例、关闭可见性的比例 |

细长与巨型构件的比例基本决定重叠退化有多严重，也就决定谁赢。预期：静态查询两者互有胜负、差距不超过一个常数因子；增量编辑那栏会一边倒，因为整树重建在百万构件上撑不住。

百万级以上还值得评估 **Morton 排序 + 分块网格**：建树与局部更新比传统 BVH 更便宜，对建筑「平面上均匀、竖向分层」的分布友好，代价是射线查询质量不如 SAH BVH。

---

## 7. 已知限制与待办

- 巨型 AABB 导致剪枝退化 → 大构件按面 / cluster 拆分。
- 中位数切分不是 SAH，混合尺度下质量差 → 大规模换 SAH / LBVH。
- `accept` 只在叶子求值 → 隐藏楼层仍被遍历，改成按分区或提前剪枝。
- 框选全量线性 + 投影近似 → 改视锥剪枝 + 叶子精判。
- `to_local_ray` 假设刚体变换 → 支持缩放 / 镜像前必须同步修正。
- 并列命中没有 tie-break → 共面重合的表面谁被选中取决于 `nth_element` 的不稳定划分，见 §2.1。
- 百万级整树重建成本未验证 → 需要 §6 的基准数据再决定是否引入 refit 或增量结构。

---

## 附录：源码锚点

| 文件 | 角色 |
|---|---|
| [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) / [picking.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.cpp) | `Bvh` 建树与 `closest_hit`；`nodes_in_screen_rect` 框选 |
| [document_viewport.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/viewport/canvas/document_viewport.cpp) | `finish_box_select`、`rebuild_bvh` 调用点、点选入口 |
| [math.h](https://github.com/terry-chao/tamias/blob/main/src/engine/math/math.h) | `Aabb` / `Frustum::from_view_proj` / `intersect_aabb` |
| [FRUSTUM-CULLING.md](FRUSTUM-CULLING.md) | 剔除分期；三期复用拾取 BVH |
| [MASSIVE-GEOMETRY.md](MASSIVE-GEOMETRY.md) | 大场景预算、拾取分层、G2 / G5 验收 |
