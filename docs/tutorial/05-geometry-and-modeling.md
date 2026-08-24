# 第 5 章　几何与造型：形状是从哪来的

> 本章目标：分清三角网 / BRep / 特征树三种几何形态，看懂「点一下 → 屏幕上出现一个盒子」的完整链路。

## 5.1 三种几何形态（先背这张表）

| 形态 | 是什么 | 优点 | 缺点 | 在 Tamias 里 |
|---|---|---|---|---|
| **三角网（Mesh）** | 一堆顶点 + 三角形 | GPU 直接能画 | 改了形状就要重新生成 | `MeshCpu` → `GpuMesh` |
| **BRep** | 精确曲面 + 拓扑边/面 | 精确、可布尔 | GPU 不认 | OCCT 内部，算完即弃（缓存） |
| **特征树（Feature Model）** | 一串带参数的步骤 | 可编辑、可存盘、可重算 | 需要求值器 | `FeatureModel`，存进 `.tdoc` |

一句话：**特征树是配方，BRep 是精确几何，三角网是渲染用的结果。** 配方是真相，后两者都是缓存。

## 5.2 一条完整链路（端到端）

```
点工具 / 拖墙
  → Command 记下位置
    → 构造 Entity：往 FeatureModel 里写特征（配方）
      → Entity::createGeom()
        → IGeometryBuilder.build()          ← 几何边界
          → evaluate_feature_model()        ← OCCT 求值
            → 逐步得到 TopoDS_Shape（BRep）
            → tessellate → MeshCpu（Z-up 转 Y-up）
      → Document::add_entity(实体, 网格)
        → MeshAsset + SceneNode
          → 视口 upload_mesh，下一帧 render_items 画出来
```

以盒子为例，构造函数只写配方，不碰 OCCT（[box_entity.cpp](https://github.com/terry-chao/tamias/blob/main/src/entity/box_entity.cpp)）：

```cpp
auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                  {{"width", 1.0}, {"height", 1.0}});
model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 1.0}});
local_transform = translate(position);
```

内存里就是一张配方表：

```
特征1  RectProfile   inputs=[]     width=1, height=1
特征2  Extrude       inputs=[1]    depth=1
放置    local_transform = 点到的位置
```

墙、梁、板、柱、门、窗都是同一套，只是参数不同（[wall_entity.cpp](https://github.com/terry-chao/tamias/blob/main/src/entity/wall_entity.cpp) 用两点算长度和朝向）。

## 5.3 求值器：照配方做菜

求值器（[occt_feature.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_feature.cpp)）拿着配方去调 OCCT：

| 特征 | OCCT 调用 | 得到 |
|---|---|---|
| RectProfile | `MakeWire` + `MakeFace` | 矩形面 |
| Extrude | `BRepPrimAPI_MakePrism` | 实心体 |
| Boolean | `Fuse / Common / Cut` | 并/交/差 |
| Fillet / Chamfer | 给第 N 条边倒圆/斜角 | 修形 |

按顺序遍历特征，每个特征引用上游结果，最后对「输出特征」（没被引用的那个）做 tessellate 变成三角网。

> 值得注意：`FeatureModel` 里**没有**任何 OCCT 类型（没有 `TopoDS_Shape`），只有 `id` / `kind` / `params`。所以配方能存盘、能换内核——这是整套架构的支点。

## 5.4 坐标系的坑：Z-up vs Y-up

OCCT 是 **Z-up**（Z 朝上），Tamias 视口是 **Y-up**（glTF/Blender 惯例）。tessellate 完最后一步转坐标：

```cpp
(x, y, z) → (x, z, -y)   // 位置和法线一起转
```

这就是为什么属性面板里「拉伸深度」看起来是「高度」——语义标签已经为此做过映射。

## 5.5 两条入口，不要混

| 入口 | 走哪 | 有没有特征树 |
|---|---|---|
| 点盒子 / 拖墙 / 改参数 | 配方 → OCCT 求值 | 有，能再改 |
| 打开 STEP / OBJ | 直接读成三角网 | 没有，改不了「原来的拉伸深度」 |

`IShapeOps`（[shape_ops.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/shape_ops.h)）就是第二条口的抽象：读 STEP/IGES/BREP → tessellate → 三角网。它和 `IGeometryBuilder`（配方口）共用同一个 OCCT，见 [几何边界](../ISHAPE-OPS.md)。

## 5.6 现在刻意没做的

- 真正的草图 + 约束求解器（现在是写死的矩形/圆）
- 增量求值（现在每次全量重算）
- 稳定的拓扑命名（圆角还引用「第 N 条边」的序号，改上游可能倒错边）

这些正是路线图 P1–P4 的内容，[第 10 章](10-web-and-roadmap.md)会讲。

## 5.7 动手练习

1. 读 [`box_entity.cpp`](https://github.com/terry-chao/tamias/blob/main/src/entity/box_entity.cpp)，把配方和 5.3 的表对上。
2. 在属性面板改盒子深度，用调试器看 `evaluate_feature_model` 被重新调用的过程。
3. 打开一个 `.obj` 和一个 `.step`，分别用句柄检查看它们的 id 差异，想想为什么。

## 延伸阅读

- [特征树求值器](../FEATURE-TREE-EVALUATOR.md)：完整版（含坐标系、缓存、三个坑）
- [几何边界](../ISHAPE-OPS.md)：内核插件口
- [MCAD 管线](../MCAD-PIPELINE.md)：还没做的 sketch → 挤出 → 拓扑命名

下一章：[文档与场景](06-document-and-scene.md)
