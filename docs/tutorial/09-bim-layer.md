# 第 9 章　BIM 业务层：建筑语义放在哪一层

> 本章目标：理解为什么建筑语义需要单独一层，看懂已落地的「门窗宿主」关系，认识 IFC 与 IfcOpenShell。

## 9.1 什么是「建筑语义」

墙、梁、板、柱、门、窗、楼层、轴网这些不是几何问题，是**建筑规则**：

- 窗开在哪面墙上（宿主）
- 这面墙属于几楼（归属）
- 轴网在哪、捕捉到哪（定位）
- 门和窗的尺寸类型（开口）

这些规则不能散落在 Qt、命令或语义树里。Tamias 为此单独设了一层：**BIM 业务层**（[`src/bim`](https://github.com/terry-chao/tamias/tree/main/src/bim)）。

## 9.2 为什么不能写进别的地方

| 若写在 | 错在哪 |
|---|---|
| `Scene` / `SceneNode` | 语义树变成 BIM 内核，盒子、STEP 零件也被逼理解「楼层」 |
| `Document::add_entity` | 通用入树口开始猜建筑规则 |
| `src/app` | Qt 里长业务，无法单测、无法给 IFC 导入复用 |
| 命令里 if-else | 墙、梁、板、窗各写一套归属，规则发散 |

所以：**内核共享，建筑规则集中**。MCAD 的盒子继续直接走 `Document`；BIM 构件多经过这一层。

## 9.3 已落地：门窗宿主关系

现在在墙上放窗/门，会写入一条 **`HostedOn` 关系**：

```
Relation
  kind        HostedOn
  from        窗 / 门
  to          墙
  placement   along / sill / offset（相对墙局部）
  valid       对齐后是否完全落在墙内
```

墙一改，管线自动跑：

```
墙参数改了（SetFeatureParamCommand）
  → notify_entity_changed(wall)
    → 按 to == wall 查出 HostedOn 的窗/门
      → 通知每个开口：
          1. 重新造型（厚度跟上墙厚）
          2. 对齐（along/sill 夹进可用范围）
          3. 合法性检查（是否仍完全落在墙内）
      → 结束
```

关系存进 `.tdoc` 的 `RELA` chunk——不靠 Z 坐标反推「窗大概在这面墙上」。显式关系才能可靠编辑。详见 [关联关系](../bim/relations.md)。

## 9.4 和语义树怎么分工

```
SceneNode.parent        ← 唯一的树真相（谁是谁的父）
BimModel 关系表          ← 另一张图（宿主图），不替代父子
```

渲染展平后**不知道**「这是 2 楼」；按楼层显隐由 bim 层查出一串 id，变成可见性标志交给渲染。

## 9.5 IFC 与 IfcOpenShell

IFC 是建筑的交换格式（类比 PDF），`.tdoc` 是编辑格式（类比 docx）。Tamias 用 IfcOpenShell 读 IFC：

- **IfcParse**（已接入）：IFC 语义——空间结构、Pset、GUID、类型/实例。`tamias_ifc_dump` 可以打印空间结构树。
- **IfcGeom**（还没接）：IFC 几何，内部仍用 OCCT。

关键认知：**IfcOpenShell 不是和 OCCT 并列的第二个几何内核**，它坐在 OCCT 上面。所以 IFC 几何将来也走 `IShapeOps` 那一条通道，不另开一条。

## 9.6 现状与将来

| 能力 | 状态 |
|---|---|
| 墙梁板柱门窗可画 | ✅ |
| 门窗宿主关系 + 自动跟随 | ✅ |
| 楼层 / 轴网 / 当前标高 | ❌（将来在 bim 层，不进 SceneNode 字段） |
| IFC 空间结构读取 | ✅ |
| IFC 几何导入 / 导出 | ❌（支撑线） |

## 9.7 动手练习

1. 放一面墙，再放一扇窗，确认它贴在墙上；然后改墙的厚度，观察窗是否跟着变厚。
2. 读 [`relation.h`](https://github.com/terry-chao/tamias/blob/main/src/bim/relation.h) 和 [`bim_model.h`](https://github.com/terry-chao/tamias/blob/main/src/bim/bim_model.h)，说出关系表和 Scene 各存什么。
3. 用 `tamias_ifc_dump` 打印一个 `.ifc` 的空间结构，对照 [IFC 空间树测试](https://github.com/terry-chao/tamias/blob/main/src/bim/ifc_spatial_tree_main.cpp)。

## 延伸阅读

- [BIM 业务层](../BIM.md)：层的位置、管什么不管什么、未来楼层设计
- [关联关系](../bim/relations.md)：数据、更新管线、RELA chunk
- [MCAD 与 BIM 决策](../DECISION-MCAD-BIM.md)：为什么 MCAD 做深、BIM 做中浅

下一章：[Web 与路线图](10-web-and-roadmap.md)
