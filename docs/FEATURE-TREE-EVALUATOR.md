# OCCT 与特征树求值器如何配合

> 一份通俗版说明：特征树（配方）怎么通过求值器翻译成 OCCT 的 BRep 操作，再离散成三角网交给渲染。面向想快速理解这套「参数化内核」数据流的人。

---

## 一句话比喻

**特征树是「配方」，OCCT 是「厨师」，求值器（evaluator）是照着配方指挥厨师的流程。**

- 配方只记「要什么」——每个步骤的类型、参数、依赖谁，全是纯数据，跟 OCCT 无关。
- 求值器把配方翻译成 OCCT 能懂的 `TopoDS_Shape` 操作。
- 算出来的 BRep（精确几何）和三角网都是**缓存**，改了参数就把配方重走一遍，缓存重新生成。

---

## 1. 三层结构

| 层 | 文件 | 职责 | 跟 OCCT 的关系 |
|---|---|---|---|
| **配方（数据）** | [feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h) `FeatureModel` | 存特征树，纯数据，能序列化进 `.tdoc` | **零依赖 OCCT** |
| **边界（抽象）** | [geom_builder.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/geom_builder.h) `IGeometryBuilder` | 只定义一个接口 `build(model) → MeshCpu` | 只声明，不实现 |
| **执行（实现）** | [occt_geom_builder.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_geom_builder.cpp) + [occt_feature.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_feature.cpp) | 真正调 OCCT 求值 | **全项目唯一碰 OCCT 几何的地方** |

最关键的一点：`FeatureModel` 里**没有任何 OCCT 类型**（没有 `TopoDS_Shape`），只有 `uint64_t id`、`FeatureKind`、`unordered_map<string,double> params`。所以它能存盘。OCCT 的 `TopoDS_Shape` 只在求值器内部活一瞬间，算完就转成 `MeshCpu` 交出去，不往外泄漏。

---

## 2. 特征树长什么样

一个特征（`feature.h` 里的 `Feature`）：

```cpp
struct Feature {
  uint64_t id;                              // 身份
  FeatureKind kind;                         // 类型：轮廓 / 拉伸 / 布尔 / 圆角 / 倒角…
  vector<uint64_t> inputs;                  // 依赖哪些上游特征的 id
  unordered_map<string, double> params;     // 命名参数，如 {"width":1.0}
};
```

一个实体（盒子）的特征树，`BoxEntity` 构造时写死（[box_entity.cpp](https://github.com/terry-chao/tamias/blob/main/src/entity/box_entity.cpp)）：

```
特征1: RectProfile   inputs=[]          params={width:1.0, height:1.0}
特征2: Extrude       inputs=[特征1]      params={depth:1.0}
```

这就是「**配方**」：一个 1×1 的矩形面，再沿 Z 拉伸 1 → 一个 1×1×1 的盒子。

**约定**：特征按「拓扑序」排列——被依赖的排在前面（第 1 步先于第 2 步）。求值器就靠这个顺序逐个算，不需要拓扑排序。

---

## 3. 求值器怎么工作

入口是 [occt_feature.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_feature.cpp) 的 `evaluate_feature_model(model, deflection)`，流程：

```
建一个 shapes 表（id → TopoDS_Shape），一开始是空的
按顺序遍历每个特征：
    根据 kind 分支，用 inputs 引用的「上游已算好的 shape」+ 自己的 params，
    调用对应的 OCCT 构造器，得到自己的 shape，存进 shapes[id]
遍历完，找到「输出特征」（output_feature() = 没被任何人引用的那个）
对输出特征的 shape 做 tessellate（离散成三角网），返回 MeshCpu
```

拿盒子走一遍：

| 步骤 | 特征 | OCCT 调用 | 得到什么 |
|---|---|---|---|
| 1 | RectProfile | `BRepBuilderAPI_MakeWire` + `MakeFace`（`make_rect_face`） | 一个矩形**面** `TopoDS_Face` |
| 2 | Extrude | `BRepPrimAPI_MakePrism(面, (0,0,depth))` | 一个实心**盒子** `TopoDS_Shape` |

然后 `output_feature()` 找到「没人引用的那个」= 特征 2（盒子），把它 tessellate 成三角网。

各特征类型 → OCCT 操作对照：

| FeatureKind | 求值器里的 OCCT 调用 | 说明 |
|---|---|---|
| `RectProfile` | `MakeWire` + `MakeFace` | 矩形轮廓面 |
| `CircleProfile` | `MakeEdge(圆)` + `MakeFace` | 圆形轮廓面 |
| `Extrude` | `BRepPrimAPI_MakePrism` | 把面沿 +Z 拉伸成体 |
| `Boolean` | `BRepAlgoAPI_Fuse / Common / Cut` | 并 / 交 / 差（`operation` 参数选） |
| `Fillet` | `BRepFilletAPI_MakeFillet.Add(radius, edge)` | 给第 N 条边倒圆角 |
| `Chamfer` | `BRepFilletAPI_MakeChamfer.Add(distance, edge)` | 给第 N 条边倒斜角 |

---

## 4. 从 BRep 到三角网（tessellate）

OCCT 算出来的 `TopoDS_Shape` 是**精确的 BRep**（参数化曲面 + 拓扑边/面），但渲染器要的是**三角网**。转换在 `tessellate_shape`：

1. `BRepMesh_IncrementalMesh` 把整个 shape 离散成三角网格。
2. 用 `TopExp_Explorer` 遍历每个面，`BRep_Tool::Triangulation` 取该面的三角数据。
3. 处理面的朝向（`REVERSED` 的面要把三角形绕序反过来，保证法线朝外）。
4. 组装成 `MeshCpu`（顶点位置 + 法线 + 索引）。

> 这里体现出「**缓存**」的设计：BRep 和三角网都能从特征树重算，所以它们不是「真相」，配方才是。

---

## 5. 坐标系转换（容易踩的坑）

OCCT 是 **Z-up**（Z 朝上），Tamias 视口是 **Y-up**（glTF/Blender 惯例，Y 朝上）。所以 tessellate 完最后一步把每个顶点绕 X 转 -90°：

```cpp
(x, y, z) → (x, z, -y)
```

位置和法线一起转。这也解释了为什么在实体层看参数含义会「错位」——比如墙的 `Extrude.depth` 在 Z-up 里是「拉深」，转到 Y-up 后就成了「高度」（属性面板的语义标签就是为这个做的）。

---

## 6. 「改参数 → 重算」闭环

以改一个盒子的 `depth` 为例（[set_feature_param_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/set_feature_param_command.cpp)）：

```
set_param(feature_id, "depth", 2.0)
   ↓ 改的是配方（FeatureModel 里的 params）
geometry_builder().build(model)   ← 重新走一遍求值器
   ↓ RectProfile 重算 → Extrude 重算 → 新 TopoDS_Shape → 新三角网
asset->cpu = 新三角网
   ↓
GPU 上传新网格 → 屏幕更新
```

整个闭环里，**用户改的始终是「配方」里的一个参数**，OCCT 只是被重新调用一遍去「重做菜」。

---

## 7. 三个关键设计点 / 坑

1. **拓扑命名（P3，还没做）**：`Fillet`/`Chamfer` 引用的是「第 N 条边」的**序号**（`nth_edge`）。改上游参数后 BRep 重算、边的编号会变，第 N 条边可能不再是原来那条 → 圆角倒错边。ROADMAP 里的「索引法 → 几何法」就是解决这个。现在是「索引法」，脆。

2. **全量重算（增量待做）**：现在 `evaluate_feature_model` 每次都从第一个特征算到最后。理想是「改了特征 3，只重算特征 3 及其下游」。当前特征树还小，全量没问题；这是 P4 及以后的优化点。

3. **配方与内核解耦的价值**：因为 `FeatureModel` 不碰 OCCT，所以：
   - 能存进 `.tdoc`（序列化只写 id/kind/params/inputs）。
   - 理论上能换内核（`IGeometryBuilder` 换实现即可），OCCT 只是当前唯一的实现。
   - 将来 IFC 导入时，声明式几何（`IfcExtrudedAreaSolid` 等）可以直接映射成特征树节点，而不是只导入三角网——这是 ROADMAP 里「IFC 可编辑」的根基。
