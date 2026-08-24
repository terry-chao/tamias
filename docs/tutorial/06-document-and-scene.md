# 第 6 章　文档与场景：数据住在哪

> 本章目标：看懂一个打开的文件（`Document`）里有哪些数据，语义树（`Scene` / `SceneNode`）怎么表达「谁是谁的孩子、谁在世界哪里」。

## 6.1 Document：一个打开的文件

`Document`（[document.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp)）是内存里的「一个 .tdoc」。它自己不造型，只持有：

| 数据 | 是什么 |
|---|---|
| `Scene` | 语义树：所有节点 + 父子 + 变换 + 包围盒 |
| 实体表 | `Entity`（参数化配方）集合，id 与 SceneNode 对应 |
| `MeshAsset` | CPU 三角网资产（`mesh_asset_id` 引用） |
| 材质 / 纹理 | `Material`、`TextureAsset` |
| `BimModel` | BIM 关系表（宿主等），作为 Document 的一个侧面 |

关键设计：**几何真相不在 app，也不在 render**。Document 持有配方和网格；渲染侧只拿平铺清单。

## 6.2 SceneNode：语义树的一个节点

打开 [`scene.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h)，节点长这样：

```cpp
struct SceneNode {
  std::uint64_t id = 0;
  std::uint64_t parent = 0;              // 0 = 根；parent 是唯一真相源
  std::vector<std::uint64_t> children;   // 派生缓存，recompute_world() 重建
  std::uint64_t mesh_asset_id = 0;       // 0 = 分组节点（无几何）
  Mat4 local_transform = Mat4::identity();  // 父空间局部变换
  Mat4 world_transform = Mat4::identity();  // 缓存：父链累积
  Aabb local_bounds{};                   // 自身几何局部包围盒
  Aabb world_bounds{};                   // 自身+子树全局包围盒
  bool selected = false;
};
```

注意三点：

1. **`parent` 是唯一真相**，`children` 是重算出来的缓存。
2. 节点只存 `mesh_asset_id` 引用，**不持有 GPU 资源**——渲染资源在渲染侧（`asset_to_gpu_`）。
3. **不解释建筑语义**。「这面墙在 2 楼」不是 SceneNode 的字段，是 bim 层算好 `parent` 写进来的。

## 6.3 变换：局部 → 世界

`Scene::recompute_world()` 做一件事（[scene.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.cpp)）：

```cpp
n->world_transform = parent_world * n->local_transform;  // 自顶向下累积
```

这就是所有 3D 引擎里「变换继承」的数学。孩子动、父亲动、孙子跟着动。同时算出 `world_bounds`（自己 + 子树），供渲染做视锥剔除和拾取。

## 6.4 Entity：可编辑的配方载体

`Entity`（[entity.h](https://github.com/terry-chao/tamias/blob/main/src/entity/entity.h)）把「特征树配方」和「场景节点」绑在一起：

```
BoxEntity / WallEntity / DoorEntity …
  ├─ FeatureModel（配方：能改、能存盘）
  └─ 对应 SceneNode（同一 id，摆放位置、颜色、选中态）
```

改参数时 id 不变，只是配方变了 → 重算 → 同一个 `mesh_asset_id` 换掉网格内容。**id 稳定**是参数化编辑体验的核心。

## 6.5 序列化：.tdoc 里有什么

`.tdoc` 是 Tamias 的工作文档（类比：IFC 是 PDF，`.tdoc` 是 docx/psd）。格式是 `TMAS` magic + chunk：

```
META / MESH / SCEN / VIEW / FEAT / MATL / TEXT / RELA
```

`FEAT` 存特征树（配方），`SCEN` 存语义树，`RELA` 存 BIM 关联关系。因为配方能序列化，所以保存再打开后，**还能继续改参数**——这是它和「导出 OBJ」的本质区别。

## 6.6 动手练习

1. 读 [`scene.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene.h) 全文，回答：`children` 为什么是缓存？
2. 打开 `.tdoc` 保存再加载，改一个参数确认还能编辑。
3. 在 `Document::render_items()` 加个断点，观察 `SceneDrawItem` 是怎么从 Scene 展平出来的（这就是第 8 章的输入）。

## 延伸阅读

- [语义树](../SCENE-GRAPH.md)：和 OCCT 场景图的对照、展平细节
- [场景图总述](../scene/index.md)：语义树 / 展平列表 / 渲染场景图三块怎么分
- [路线图](../ROADMAP.md) 第 1 节：格式分工（`.tdoc` vs IFC vs STEP）

下一章：[命令与撤销](07-commands-and-undo.md)
