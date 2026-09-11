# 超大规模三角：虚拟离散流水线

> 场景管理里「几十亿三角」怎么扛。结论对应当前代码。语义树 / 展平 / 渲染场景图见 [SCENE-GRAPH.md](SCENE-GRAPH.md)；一期视锥见 [视锥剔除](FRUSTUM-CULLING.md)；BRep → 三角见 [特征树求值器](FEATURE-TREE-EVALUATOR.md) 第 5 节。

---

## 0. 结论先行（TL;DR）

**不能优化「画出几十亿三角」。必须让几十亿三角永远不以全分辨率驻留显存，也不以「一物体一次 draw」提交。**

1080p 大约 200 万像素，4K 大约 830 万。一帧里真正贡献颜色的三角，数量级是「像素的几倍」，不是「模型里有多少」。企业级产品（CATIA CGR / JT / Navisworks / Cesium 3D Tiles / Nanite）做的是同一件事：**把「模型里的三角」和「这一帧提交的三角」拆开。**

| 问题 | 结论 |
|---|---|
| 几十亿三角能塞进 GPU 吗？ | **不能。** 以 Tamias 当前 `Vertex`（44 字节）计，10 亿三角即使焊接后也是几十 GB，工作站显存装不下，更不可能 CPU+GPU 各留一份。 |
| Tamias 现在会先卡在哪？ | **语义树规模与 BVH 视锥二期**，不是「没合批」。G1 已 intern + GPU instancing；G3 录制期选档；G4 异步 upload + 显存 LRU。一万根同型号柱近处 1 次 draw。 |
| 几何源头该是什么？ | **BRep / 特征树是真相，三角是缓存。** CAD 比游戏更适合「按屏幕误差重离散」，不必先上 Nanite。 |
| 渲染侧该拿什么？ | 仍是展平投影 + 加速结构。**不要把语义树复制进 GPU。LOD 不是场景图节点。** 空间 BVH、实例表、LOD cluster 都不是场景图。 |
| 一帧的硬顶是什么？ | **驻留三角受屏幕约束**（约 1000–3000 万提交、2000 次以内 draw），模型规模无上限，靠流式 + LOD。 |

---

## 1. 为什么「优化三角」是错的提法

显示器不会画「几十亿」。它给每个像素一个颜色。超过每个像素大约 1–4 个三角之后，多出来的全是顶点变换白做、片元互相覆盖、以及 CPU 发 draw。

规模问题拆成三句，顺序不能反：

1. **少生成**：远处、隐藏楼层、关掉的专业，不要用 0.05 的 deflection 离散。
2. **少驻留**：显存是 LRU 预算，不是「文档里有的网格全 upload」。
3. **少提交**：同几何用 instancing；屏外 / 小于 1 像素 / 被楼挡住的不发 draw。

「把 MeshCpu 再压一遍」只解决第 2 句的边角，解决不了第 1 和第 3。

---

## 2. 贴着现有代码：现在会在哪爆

| 环节 | 现状 | 几十亿时还缺什么 |
|---|---|---|
| 离散 | 特征预 intern L1（粗 deflection）；STEP/IGES/BREP 开档只算 `Shape::bounds()`，按 `LodRequest` 在 OCCT 单 worker 里懒离散 | 隐藏楼层不打网已落地；整网 L1/L2，Face cluster 只留了范围，尚未按面流式 |
| 资产 | `intern_mesh` 按指纹共享；`TessCache` 把 `(geometry_id, MeshLod)` 映射到具体 `MeshAsset` | 三角汤（demo cube / 已拍扁导入网）不能重离散，只有 L0 + 原网 |
| 语义树 | `Scene::find` 线性扫；`recompute_world()` 全量 | 十万节点时，改一根柱也扫全树 |
| 展平 | `render_items()` 全量清单（无视锥），好让留存树完整 | 树扁时仍是 O(构件数)；清单本身可以上百万条 |
| 录制 | `RecordCommands` 视锥 + 隐藏 + 屏幕误差选档；合批键是 `gpu_mesh_id`，同几何同档 instance | 远近不同档自然拆成两批；G6 MDI 还没做 |
| 上传 | `request_upload_mesh` 提交即返回；缺网画已驻留的更粗档或 L0 盒 | 打开大 STEP 首帧出盒/粗网，L2 流式补 |
| 驻留 | `ResidentCache` 默认 2GB LRU，淘汰 GPU buffer | 非编辑对象的 CPU `MeshCpu` 仍留在 Document |
| 拾取 | 物体级 BVH；叶子打 `resolved_mesh`（Work/Close，不打 L0 盒当边） | 单个 5000 万三角零件点选仍会卡；拾取树不参与绘制剔除 |
| RHI | `instance_count` + instance 缓冲已接 | G6 indirect 仍未做 |

一句话：**G1/G3/G4 已经把「一物体一份全精度网 + 一次 draw + 阻塞 upload」拆开了。** 剩下的硬顶是扁平语义树、无 BRep 的超大三角汤（G5）、以及 CPU 录两万次 draw（G6）。

---

## 3. 内存账（为什么必须虚拟化）

当前 `Vertex`：`position(12) + normal(12) + uv(8) + color(12)` = **44 字节**。索引 `uint32`。

| 三角数 | 顶点（按 0.5 顶点/三角焊接估） | 仅 GPU（顶点+索引） | CPU 再留一份 `MeshCpu` |
|---|---|---|---|
| 100 万 | 22 MB + 12 MB ≈ 34 MB | 可忽略 | 可忽略 |
| 1 亿 | 2.2 GB + 1.2 GB ≈ 3.4 GB | 中档卡勉强 | 再 ×2 → 工作站吃紧 |
| 10 亿 | 22 GB + 12 GB ≈ 34 GB | **装不下** | 不必谈 |

屏幕预算（企业产品实际在画的）：

| 分辨率 | 像素 | 目标「提交三角」（含 2–4× overdraw） | 目标 draw |
|---|---|---|---|
| 1080p | ~2.1e6 | 8e6–2e7 | < 2000 |
| 4K | ~8.3e6 | 2e7–4e7 | < 2000 |

**模型可以有 10 亿三角的「逻辑几何」；显存里同时活着的，应永远落在上表右列。** 多出来的只存在：BRep、磁盘上的离散缓存、尚未加载的 cluster。

---

## 4. 企业级架构：虚拟离散流水线

不引入第二棵语义树。继续遵守 [SCENE-GRAPH.md](SCENE-GRAPH.md) 的边界：**`Scene` 是层级唯一真相源；渲染侧是绘制投影；空间索引不是场景图。**

在现有「语义树 → 展平 → 留存场景图 → RecordCommands」上，加三样东西，只加在几何与提交两侧：

```
特征树 / STEP / IFC
        │ 求值
        ▼
   BRep（精确，可重建）                    Scene（谁属于谁 + 变换）
        │ 按屏幕误差离散，按 Face/cluster 切片     │ 实例：多节点 → 同一 geometry_id
        ▼                                         │
 TessCache（LOD0/1/2，量化，可落盘）               │ dirty + 可见性（楼层/专业）
        │                                         ▼
        │                              空间 BVH + 语义剪枝（二期/三期）
        │                                         │ 本帧需要的 (geometry, lod, cluster)
        └──────────── 驻留管理 LRU ────────────────┘
                          │ 流式 upload，有显存预算
                          ▼
                   合批键：(mesh, lod, pipeline, 材质桶)
                          │ instancing / MultiDrawIndirect
                          ▼
                   GPU：这一帧真正画的 千万级三角
```

四条不变的规则：

1. **语义节点不持三角。** `SceneNode` 继续只引用 `mesh_asset_id`（将来是 `geometry_id` + 可选 override）。
2. **一份几何，N 个实例。** IFC `IfcWallType` / 族 / 相同参数的柱，必须共享同一份 tessellation。变换和材质 override 在实例上。
3. **三角是多分辨率缓存。** 改参数 → 丢相关 LOD → 按当前相机误差重离散。不要把「最高精度网」当文档主数据。
4. **驻留有预算。** 超过预算淘汰最久未见的 cluster；CPU 上非编辑对象可不留 `MeshCpu`，需要时从 BRep 或磁盘缓存重建。

---

## 5. 六层手段（按 ROI，不是按酷）

游戏引擎会先上 Nanite。Tamias 是 MCAD/BIM，**BRep 还在**，顺序必须反过来：先复用几何、先别画看不见的、先按距离换粗网，最后才对「单个巨型三角汤」（扫描网格、无 BRep 的 OBJ）做 cluster DAG。

### G1 实例化 + 合批（最高 ROI）

完整企业级方案（三层模型、合批键、实例布局、RHI 契约、分期 G1a–d）见 **[合批 / Instancing](INSTANCING.md)**。这里只留总图里的位置。

BIM 里「三角多」经常是「同一段墙截面复制了八千次」。G1 先 intern 再 instance：一万根同型号柱 = **1 份网格 + 1 次 draw**。合批不要按语义父子。

### G2 层级剔除（设计已有，代码差二期/三期）

[视锥剔除](FRUSTUM-CULLING.md) 一期已落地（叶子 AABB × 六平面）。还要：

- **二期**：分组节点 `world_bounds` 整盒在外 → 子树不测。前提是 BIM 层真把墙挂到楼层上；全 `parent=0` 时二期等于一期。
- **三期**：复用拾取 `Bvh` 做空间剪枝，专治扁平十万散件。
- **像素剔除**：投影 AABB 短边 < 1–2 px 的叶子直接跳过（或降到 impostor）。这是「几十亿」时比视锥更狠的一刀。
- **遮挡**：放在合批之后。室内 BIM 用 Hi-Z / 两阶段遮挡查询；不要在 G1 之前做。

目标：镜头在一层房间里，整栋十万构件 → 可见集合几百到几千。

### G3 自适应离散（已落地选档 + 懒离散；Face 流式后做）

**LOD 不是场景图节点，也不是 `SceneNode` 字段。** `mesh_asset_id` 仍是几何身份；相机一动换网如果写进 `render_items()` / 脏标记，留存树每帧都会被打脏。档次选择发生在 `RecordCommands`（已有 `eye_position`、视锥、`Drawable.bounds`）。合批键已经是 `gpu_mesh_id`：同几何同档仍 instance；远近不同档自然拆成两批。

固定 `deflection = 0.05` 只作为 **L2 工作网**。屏幕误差（与 `grid_snap_world_radius` 同源）：

```
world_per_pixel ≈ 2 * tan(fovy/2) * dist(eye, aabb_center) / framebuffer_height
projected_px ≈ aabb_world_short_edge / world_per_pixel
```

落地档次（离散四档，不用每帧连续 deflection）：

| LOD | 何时 | 怎么来 |
|---|---|---|
| L0 Box | 投影短边 < ~4px（<1px 直接不画） | 共享单位盒 instance，缩放到世界 AABB |
| L1 Coarse | 远景（< ~80px） | 特征约 12× 工作 deflection 再 tessellate；导入约 1.6 |
| L2 Work | 视口里正常编辑 | 特征 0.05，导入 0.1 |
| L3 Close | 选中 / 剖面 / 测量 | 更小 deflection，接口已留，后做 |

滞回在渲染线程 `ChannelState.lod_by_node`（升档 1.25×、降档 0.8×）。草图 `line_list` **永不 LOD**。隐藏集（楼层 / 类别 / isolate）本来就不录制，也就不发 tessellate 请求。选中至少抬到 L2。

几何源：特征走 `Entity::createGeom(deflection)`（不缓存 BRep）；CAD 导入 Document 持有 `unique_ptr<Shape>`；三角汤只有 L0 + 原网。改参数走 `replace_entity_mesh`：该 `geometry_id` 无场景引用时丢掉全部 LOD 缓存。

`tessellate_shape` 已按 Face 记下 `MeshCpu.faces` 范围与每面 AABB，**不改顶点格式、不改 `.tdoc` 序列化**。远距离按面剔除 / 剖切按面加载是下一刀，不要和整网 L1/L2 绑死。

### G4 压缩与驻留预算（异步 upload + LRU 已落地；量化后做）

- GPU：`ResidentCache` 默认 2GB，LRU 淘汰 buffer。`request_upload_mesh` 提交即返回；`asset_to_gpu_` 有了才能画细档，否则继续粗档 / L0 盒。
- 调试 overlay（坐标读数第二行）：draw 数、提交三角、GPU 网格字节、pending tessellate。
- 顶点相对 AABB 量化为 16-bit、法线 octahedron、去掉每顶点颜色：**还没做**。
- 非正在编辑的对象，upload 后丢 CPU 副本：**还没做**；点选走 `resolved_mesh`（工作网），精确查询走 BRep / 特征。
- 文档侧：大 tessellation 缓存跟 [ROADMAP.md](ROADMAP.md) 已定的 LevelDB 走，不要塞进整文件 `binary_archive`。

### G5 超大三角汤（无 BRep 的导入网）

扫描网格、超大 OBJ/GLB、无参数的网格零件：没有「再 tessellate 一次」这条退路。

- 切 meshlet（每簇 64–128 三角）+ 簇包围盒 + 锥体（backface cluster cull）。
- 视锥 / 像素误差在 cluster 级做；只有可见簇进驻留。
- 这是 Nanite 的简化子集：**先 cluster + 流式，不上 DAG 自适应细分**，直到 G3 对 BRep 仍不够。

单个 5000 万三角的扫描件，也应能在「只看见一角」时只驻留几百万。

### G6 GPU-driven（Vulkan 先，可降级）

CPU 录两万次 draw 仍会顶满。下一步：

- 可见实例写入 GPU buffer，`vkCmdDrawIndexedIndirectCount` / `glMultiDrawElementsIndirect`。
- 可选 compute：视锥 + 锥体剔除在 GPU。
- Mesh shader 作为 Vulkan 后端加成，**不是 RHI 抽象的新动词**；OpenGL/WebGL 继续走 CPU 分桶 + instancing。

不要为 G6 改语义树。

---

## 6. 拾取与编辑：三角不是交互对象

大规模下「对全精度网打射线」和「画出全精度网」一样错。

| 操作 | 走哪一层 |
|---|---|
| 点选构件 | 物体 BVH →（可选）cluster / Face 盒 → 命中 `node_id` |
| 点选面/边（建模） | 粗网定位到 Face，**精确查询走 OCCT BRep**，不扫几百万三角 |
| 框选 | 已有屏幕 AABB；大规模时先用 BVH 剪枝 |
| 改参数 | 特征树求值 → 丢该 `geometry_id` 的 LOD 缓存 → 按当前误差重离散 → 增量 upload |
| GPU 选色拾取 | 仅查看器路径；编辑器仍要稳定 ID，不能只靠像素 |

编辑路径永远打到特征 / BRep，不打到「当前这一帧的 LOD 三角」。否则一改相机，拓扑引用就漂。

---

## 7. 分期与验收（可执行）

与路线图「大模型渲染」对齐。G1 / G3a–b / G4 已落地；G5 前不要开工 Nanite。

| 阶段 | 内容 | 验收（比感觉重要） |
|---|---|---|
| **G0 仪表** | 每帧：draw 数、提交三角、GPU 网格字节、pending tessellate | 视口坐标读数第二行能读到 |
| **G1 实例+合批** | 共享 `mesh_asset_id`；分桶 + `instance_count`；instance 矩阵缓冲 | 1 万同几何实例 ≤ 个位数 draw；帧时间不再随实例数线性涨 |
| **G2 剔除补全** | 语义树剪枝 + BVH 视锥；亚像素丢弃已在录制期（<1px skip） | 十万扁平散件、镜头看一角：提交集合掉一个数量级 |
| **G3 自适应离散** | 屏幕误差 → 离散四档；L1 预 intern / 懒离散；Face 范围已记录 | 同一 STEP：远景三角数随距离下降；拉近不破洞（滞回） |
| **G4 驻留** | 显存预算 LRU；异步 upload；缺网降粗档 | 打开超大模型不冻 UI；显存曲线有顶 |
| **G5 meshlet** | 无 BRep 大网切簇 | 单网格 5e7 三角，只看见局部时 GPU 网远小于全量 |
| **G6 indirect** | Vulkan MultiDrawIndirect；GL 保持 G1 | 可见实例过万时 CPU 录制不再是主因 |

建议的内部 SLO（工作站，1080p，着色模式）：

- 导航：≥ 30 fps（目标 60）
- 提交三角：< 2e7
- draw：< 2000
- 显存网格：< 设定预算（建议 2 GB 起步，可配置）
- 打开文档：首帧先出 L1 / 包围盒，L2 流式补上，禁止同步 tessellate 整楼

---

## 8. 明确不做（避免把方案做歪）

- **不把语义树搬进 GPU。** 楼层、GUID、Pset 继续只在 Document / BIM。
- **不把 LOD 写成场景图节点。** 档次是录制期策略 + 几何缓存，不进 `SceneNode`，相机动不 `bump_generation`。
- **不先做 Nanite。** 有 BRep 时，重离散比 cluster DAG 便宜、可编辑、能对上特征树。
- **不为 IFC 另开一条三角通道。** 几何仍走 OCCT；IFC 只提供实例与共享关系（`IfcMappedItem` → `mesh_asset_id` 复用）。
- **不建三套空间树当真相源。** 语义树管归属；一张 BVH 管空间。Octree 不是必须的。
- **不把最高精度网写进 `.tdoc` 当主数据。** 三角是缓存；主数据是特征 / BRep / 实例变换。
- **不以「三角形计数器变小」当成功。** 成功是：draw 降下来、驻留有顶、远景自动变粗、打开大文件不卡死。

---

## 附录：源码锚点

| 文件 | 现在干什么 | 方案落点 |
|---|---|---|
| [mesh.h](https://github.com/terry-chao/tamias/blob/main/src/engine/graphics/mesh.h) `Vertex` / `MeshCpu` | 44 字节顶点；`faces` 为 Face 范围 + AABB | 量化顶点；按面流式加载 |
| [mesh_lod.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/mesh_lod.h) | `MeshLod` + `select_mesh_lod` 滞回 | 连续 deflection / Close 档 |
| [tess_cache.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/tess_cache.h) | `(geometry_id, lod) → MeshAsset` | 落盘 |
| [occt_feature.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_feature.cpp) `tessellate_shape` | 按 Face 出网并记下范围 | 按 deflection 多档 + 按面剔除 |
| [tess_worker.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/tess_worker.cpp) | OCCT 单线程队列 | — |
| [mesh_asset.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/mesh_asset.h) | 一资产一份 CPU 网；intern 按指纹 | 引用计数 / 几何指纹复用 |
| [scene.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) | 语义树，线性 find；`mesh_asset_id` = 几何身份 | find 改索引 |
| [document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) | 持有导入 `Shape`；`replace_entity_mesh` 失效 LOD | BVH 查询产出清单 |
| [scene_graph.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/scene_graph.cpp) `RecordCommands` | 视锥 + 选档 + 合批 | G6 MDI |
| [resident_cache.h](https://github.com/terry-chao/tamias/blob/main/src/engine/render/resident_cache.h) | GPU LRU 2GB | 可配预算；丢 CPU 副本 |
| [render_runtime.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_runtime.cpp) | `request_upload_mesh` 非阻塞；缺网降档 | — |
| [picking.h](https://github.com/terry-chao/tamias/blob/main/src/engine/document/picking.h) | 物体 BVH + `resolved_mesh` | G2 复用视锥；大网加 cluster 层 |
| [FRUSTUM-CULLING.md](FRUSTUM-CULLING.md) | 一期已落地 | G2 的设计原文 |
| [ROADMAP.md](ROADMAP.md) §5 / 支撑线 | 「大模型渲染」 | 本文把那句话拆成 G0–G6 |
