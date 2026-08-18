# 几何边界（IShapeOps）

> 造型里的**内核插件口**。特征树、场景、BIM 业务、app **不出现** `TopoDS_Shape`。OCCT 和将来的 IfcOpenShell 都从这里接进去，**不要为 IFC 另开一条几何通道**。IFC 的空间结构（楼层等）进 [BIM 业务层](BIM.md)，几何仍只走本口。站点导航在 [造型](modeling/index.md) 下面。

现在怎么造型（点工具 → 配方 → 出网）见 [特征树求值器](FEATURE-TREE-EVALUATOR.md) 第 1 节。本文只讲「口在哪、谁实现、IFC 怎么坐上来」。

---

## 这一层解决什么

CAD 内核（OCCT）类型又重又专。若 `Document`、特征树、Qt 面板都直接 `#include` OCCT，换内核或加 IFC 会把整个工程绑死。

所以划一条线：

```
app / document / 特征树（纯数据）
        │  只认抽象接口
        ▼
   几何边界：IShapeOps · IGeometryBuilder · Shape
        │  只有这里碰 OCCT
        ▼
   OcctShapeOps / OcctGeometryBuilder  →  TopoDS_Shape → 三角网
```

接口在 [`shape_ops.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/shape_ops.h)、[`geom_builder.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/geom_builder.h)。OCCT 实现在 `occt_shape_ops.cpp`、`occt_geom_builder.cpp`、`occt_feature.cpp`。

---

## 两个口，一个内核

这一层现在有两扇门，都由 **同一套 OCCT** 实现：

| 接口 | 方向 | 干什么 | 谁在用 |
|---|---|---|---|
| `IShapeOps` | **文件 → 几何** | `read_file` 读 STEP / IGES / BREP，得到 `Shape`，再 `tessellate` 成三角网 | 打开交换文件（[`main_window.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/main_window.cpp)） |
| `IGeometryBuilder` | **配方 → 几何** | `build(FeatureModel)`：求值特征树 → BRep → 三角网 | 参数化实体改参数后重算 |

`Shape` 是内核无关的句柄：`backend_name()` + `tessellate()`。OCCT 实现里它内部握着 `TopoDS_Shape`，出边界只交出 `MeshCpu`。

头文件里还留着占位 `MeshShape`（纯三角、不经 OCCT）。OBJ / GLB 不走 `IShapeOps`，走自己的 mesh loader。

---

## 造型里面的两截

文档导航里几何边界属于造型；代码上仍要分开，免得 `TopoDS_Shape` 漏到特征树和 UI。

| | 特征树 / 求值配方 | 几何边界（本页） |
|---|---|---|
| 记什么 | kind / params / 依赖 | 内核怎么把配方或文件变成 BRep |
| 依赖 OCCT？ | **否**（`FeatureModel` 零 OCCT 类型） | **是**（唯一允许碰 `TopoDS_Shape` 的地方） |
| 换厨师 | 配方不用改 | 换 `IShapeOps` / `IGeometryBuilder` 的实现 |

求值器是两截的接缝：配方交出去，边界里的 OCCT 实现去执行。细节仍看 [特征树与求值器](FEATURE-TREE-EVALUATOR.md)。

---

## OCCT 现在接了什么

- **读文件**：`OcctShapeOps` 注册名 `"occt"`。STEP（含 XCAF 颜色/名称）、IGES、BREP。
- **做特征**：`OcctGeometryBuilder` → `evaluate_feature_model`（拉伸、布尔、圆角、倒角等）。
- **离散**：`BRepMesh_IncrementalMesh` → `MeshCpu`；OCCT 是 Z-up，出网前转到视口的 Y-up。

启动时登记 OCCT 的 ShapeOps；打开 `.step` 等扩展名时 `ShapeOpsRegistry::find("occt")`。

---

## IfcOpenShell 怎么上来

IfcOpenShell **不是**和 OCCT 并列的第二个几何内核。它是：

- `IfcParse`：IFC 语义（空间结构、Pset、GUID、类型/实例）——**已接入**，读 `.ifc` 可打印空间结构树（`tamias_ifc_dump` / 打开文件）
- `IfcGeom`：几何，**内部仍用 OCCT 7.9.3**——还没接

所以 IFC 导入应当：语义进语义树，几何仍经 OCCT 变成 BRep / 三角网（能映射成特征树的更好）。**不要**再写一条「IFC → 三角网」绕过本层。

现状缺口（[路线图](ROADMAP.md)）：`IShapeOps` 现在只吐 `MeshCpu`，扛不住 IFC 语义，也扛不住「导入成特征树」。需要专用导入器，产出「语义树 + 特征树/网格 + 材质」，几何仍走 OCCT。

---

## 现在刻意没做的

- IfcGeom / 把 IFC 几何送进文档
- `Shape` 升级为「持有特征树 + evaluate()」（路线图第 8 节仍写着）
- 第二套几何内核（Truck 等）；接口预留了，没有第二实现

附录：[shape_ops.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/shape_ops.h) · [occt_shape_ops.cpp](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/occt_shape_ops.cpp) · [geom_builder.h](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/geom_builder.h)
