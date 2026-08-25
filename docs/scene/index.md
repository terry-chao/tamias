# 场景图

谁属于谁、怎么变换、怎么交给渲染。文档导航里「场景图」是这一层的**总称**，不是把语义树改名叫 scene graph。下面再拆三块，不要混：

| 块 | 管什么 | 现状 |
|---|---|---|
| **语义树** | 通用父子树、局部/世界变换、包围盒 | `Scene` / `SceneNode`，已落地。楼层/轴网规则在 [BIM 业务层](../BIM.md)，现状无楼层，墙 `parent=0` |
| **展平列表** | 语义树烘成「网格 + 全局矩阵」 | `render_items()` → `SceneDrawItem` |
| **渲染场景图** | 面向绘制的节点树（VSG 式） | **已落地**：`StateGroup`/`StateCommands` + `RecordCommands` 访问者；由语义树脏标记增量同步（`Scene::dirty_since` → `FrameSubmission` → `update_scene_graph`），整树保留在渲染线程，不再每帧重建 |

语义树是层级的唯一真相源；渲染侧只拿展平结果（或将来从语义树增量同步出的绘制投影）。空间索引 / BVH 不是场景图，见 [视锥剔除](../FRUSTUM-CULLING.md)。

语义树、展平、渲染场景图（含代码实现地图）都写在 [SCENE-GRAPH.md](../SCENE-GRAPH.md) 同一篇里，将来按需再拆。

- [语义树](../SCENE-GRAPH.md) —— 和 OCCT 对照、变换累积、展平；「实现地图」一节说明场景图散落在哪些文件、每帧怎么流转
