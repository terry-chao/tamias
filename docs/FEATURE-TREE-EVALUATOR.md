# OCCT 与特征树求值器如何配合

> 一份通俗版说明：特征树（配方）怎么通过求值器翻译成 OCCT 的 BRep 操作，再离散成三角网交给渲染。内核插件口（`IShapeOps` / IfcOpenShell）见 [几何边界](ISHAPE-OPS.md)。

---

## 一句话比喻

**特征树是「配方」，OCCT 是「厨师」，求值器（evaluator）是照着配方指挥厨师的流程。**

- 配方只记「要什么」——每个步骤的类型、参数、依赖谁，全是纯数据，跟 OCCT 无关。
- 求值器把配方翻译成 OCCT 能懂的 `TopoDS_Shape` 操作。
- 算出来的 BRep（精确几何）和三角网都是**缓存**，改了参数就把配方重走一遍，缓存重新生成。

现在没有草图约束求解器。盒子、墙、柱都是「矩形/圆 + 拉伸」写死在实体构造函数里。将来的 sketch → 挤出 → 倒角见 [MCAD 管线](MCAD-PIPELINE.md)。

---

## 1. 现在具体怎么造型（端到端）

一条固定闭环：**先写配方，再让 OCCT 按配方做出三角网，再挂进文档去画。**

```
点工具 / 拖墙
  → Command 记下位置
    → 构造 Entity：往 FeatureModel 里写特征（配方）
      → Entity::createGeom()
        → IGeometryBuilder.build()          ← 几何边界
          → evaluate_feature_model()        ← OCCT 求值
            → 逐步得到 TopoDS_Shape
            → tessellate → MeshCpu（Z-up 转到 Y-up）
      → Document::add_entity(实体, 网格)
        → MeshAsset + SceneNode（同一 id）
          → 视口 upload_mesh，下一帧 render_items 画出来
```

[`Document`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document.cpp) **自己不做造型**，只收已经求好的实体和网格。

### 1.1 点一下：先有配方，还没有实体形状

以盒子为例。工具武装后点地面，[`CreatePrimitiveCommand`](https://github.com/terry-chao/tamias/blob/main/src/command/create_primitive_command.cpp) 用点击位置构造 `BoxEntity`。构造函数只写特征树，不调 OCCT：

```cpp
// box_entity.cpp
auto& profile = model.add_feature(FeatureKind::RectProfile, {},
                                  {{"width", 1.0}, {"height", 1.0}});
model.add_feature(FeatureKind::Extrude, {profile.id}, {{"depth", 1.0}});
local_transform = translate(position);
```

内存里是：

```
特征1  RectProfile   inputs=[]     width=1, height=1
特征2  Extrude       inputs=[1]    depth=1
放置    local_transform = 点到的位置
```

墙同理：两点算出长度、中点、朝向，配方仍是 `RectProfile(厚×长) + Extrude(高)`，变换是平移到中点再绕 Y 转（[wall_entity.cpp](https://github.com/terry-chao/tamias/blob/main/src/entity/wall_entity.cpp)）。柱、板、门、窗都是这一套，只是默认尺寸不同。

### 1.2 createGeom：配方交给几何边界

[`Entity::createGeom`](https://github.com/terry-chao/tamias/blob/main/src/entity/entity.cpp) 只做一件事：

```cpp
return geometry_builder().build(model, deflection);
```

`geometry_builder()` 是全局的 `OcctGeometryBuilder`，里面就是 `evaluate_feature_model(model, 0.05)`。`0.05` 是离散精度。实体层仍然看不到 `TopoDS_Shape`。口在哪见 [几何边界](ISHAPE-OPS.md)。

### 1.3 求值器：按顺序把每一步做成 BRep

细节见下面第 4 节。盒子走两步：矩形面 → 沿 Z 拉伸 → 实心盒子。只对**输出特征**（没被别人引用的那个）tessellate，BRep 算完就丢，不存进文档。

### 1.4 进文档：网格和场景节点绑在一起

`createGeom` 成功后：`document_->add_entity(盒子实体, 三角网)`。

1. 三角网放进 `MeshAsset`
2. 建 `SceneNode`：名字、`mesh_asset_id`、局部变换
3. **实体 id = 场景节点 id**，再 `recompute_world()` 算世界矩阵和包围盒

下一帧视口 `upload_mesh`，`render_items()` 带上世界矩阵去画。渲染不知道这是盒子。上屏见 [渲染管线](RENDERING.md)。

### 1.5 改参数：只改配方，整棵树重算

属性面板改 `depth` → [`SetFeatureParamCommand`](https://github.com/terry-chao/tamias/blob/main/src/command/set_feature_param_command.cpp)：

```
entity->model.set_param(feature_id, "depth", 2.0)   // 只改配方
geometry_builder().build(entity->model)             // 全量再求值
asset->cpu = 新三角网                               // 同一 mesh id 换内容
recompute_scene()
```

视口发现网格脏了就重新 upload。屏幕上的盒子变高，id 没变。

加圆角：[`AddFeatureCommand`](https://github.com/terry-chao/tamias/blob/main/src/command/add_feature_command.cpp) 在当前输出特征后面再挂一个 `Fillet`，再 `build` 一次。布尔则是把另一棵特征树 `append` 进来再加 `Boolean` 节点。

### 1.6 两条入口不要混

| 入口 | 走哪 | 有没有特征树 |
|---|---|---|
| 点盒子 / 拖墙 / 改参数 | 上面这条：配方 → OCCT 求值 | 有，能再改 |
| 打开 STEP | `IShapeOps.read_file` → tessellate | **没有**，只是一坨三角，改不了「原来的拉伸深度」 |

### 1.7 现在刻意没做的

- 真正的草图 + 约束（现在是写死的矩形/圆）
- 增量求值（每次从第一个特征算到最后）
- 稳的拓扑命名（圆角仍是「第 0 条边」）
- 导入 STEP 反建成特征树

---

## 2. 三层结构

| 层 | 文件 | 职责 | 跟 OCCT 的关系 |
|---|---|---|---|
| **配方（数据）** | [feature.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/feature.h) `FeatureModel` | 存特征树，纯数据，能序列化进 `.tdoc` | **零依赖 OCCT** |
| **边界（抽象）** | [geom_builder.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/geom_builder.h) `IGeometryBuilder` | 只定义一个接口 `build(model) → MeshCpu` | 只声明，不实现 |
| **执行（实现）** | [occt_geom_builder.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_geom_builder.cpp) + [occt_feature.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_feature.cpp) | 真正调 OCCT 求值 | **全项目唯一碰 OCCT 几何的地方** |

最关键的一点：`FeatureModel` 里**没有任何 OCCT 类型**（没有 `TopoDS_Shape`），只有 `uint64_t id`、`FeatureKind`、`unordered_map<string,double> params`。所以它能存盘。OCCT 的 `TopoDS_Shape` 只在求值器内部活一瞬间，算完就转成 `MeshCpu` 交出去，不往外泄漏。

---

## 3. 特征树长什么样

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

## 4. 求值器怎么工作

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

## 5. 从 BRep 到三角网（tessellate）

OCCT 算出来的 `TopoDS_Shape` 是**精确的 BRep**（参数化曲面 + 拓扑边/面），但渲染器要的是**三角网**。转换在 `tessellate_shape`：

1. `BRepMesh_IncrementalMesh` 把整个 shape 离散成三角网格。
2. 用 `TopExp_Explorer` 遍历每个面，`BRep_Tool::Triangulation` 取该面的三角数据。
3. 处理面的朝向（`REVERSED` 的面要把三角形绕序反过来，保证法线朝外）。
4. 组装成 `MeshCpu`（顶点位置 + 法线 + 索引）。

> 这里体现出「**缓存**」的设计：BRep 和三角网都能从特征树重算，所以它们不是「真相」，配方才是。

---

## 6. 坐标系转换（容易踩的坑）

OCCT 是 **Z-up**（Z 朝上），Tamias 视口是 **Y-up**（glTF/Blender 惯例，Y 朝上）。所以 tessellate 完最后一步把每个顶点绕 X 转 -90°：

```cpp
(x, y, z) → (x, z, -y)
```

位置和法线一起转。这也解释了为什么在实体层看参数含义会「错位」——比如墙的 `Extrude.depth` 在 Z-up 里是「拉深」，转到 Y-up 后就成了「高度」（属性面板的语义标签就是为这个做的）。

---

## 7. 「改参数 → 重算」闭环

端到端见上文 [第 1.5 节](#1-现在具体怎么造型端到端)。要点只有一句：用户改的始终是配方里的一个数，OCCT 被重新调用一遍去「重做菜」，同一 `mesh_asset_id` 换掉 `MeshCpu`。三角网上屏见 [渲染管线](RENDERING.md)。

---

## 8. 三个关键设计点 / 坑

1. **拓扑命名（P3，还没做）**：`Fillet`/`Chamfer` 引用的是「第 N 条边」的**序号**（`nth_edge`）。改上游参数后 BRep 重算、边的编号会变，第 N 条边可能不再是原来那条 → 圆角倒错边。ROADMAP 里的「索引法 → 几何法」就是解决这个。现在是「索引法」，脆。

2. **全量重算（增量待做）**：现在 `evaluate_feature_model` 每次都从第一个特征算到最后。理想是「改了特征 3，只重算特征 3 及其下游」。当前特征树还小，全量没问题；这是 P4 及以后的优化点。

3. **配方与内核解耦的价值**：因为 `FeatureModel` 不碰 OCCT，所以：
   - 能存进 `.tdoc`（序列化只写 id/kind/params/inputs）。
   - 理论上能换内核（`IGeometryBuilder` 换实现即可），OCCT 只是当前唯一的实现。
   - 将来 IFC 导入时，声明式几何（`IfcExtrudedAreaSolid` 等）可以直接映射成特征树节点，而不是只导入三角网——这是 ROADMAP 里「IFC 可编辑」的根基。
