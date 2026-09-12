# 墙-墙交接（斜接倒角）

> 两面墙在端点相接时，不再是两段长方体硬拼，而是**共用一个竖直斜接面、各自裁掉**——现实里的墙转角就是这样。

以前两段墙只是硬拼：外角会缺一个方块（两个盒子都到不了的位置），而且两墙在接缝处的侧面正好共面，渲染上互相闪。现在建墙、挪墙、改墙厚都会算出交接，自动斜接。

代码在 [`src/bim/wall_join.h`](https://github.com/terry-chao/tamias/blob/main/src/bim/wall_join.h) / [`wall_join.cpp`](https://github.com/terry-chao/tamias/blob/main/src/bim/wall_join.cpp)。

---

## 1. 交接怎么判

只看墙的两端（`LineLocation` 的起点 / 终点），容差 1mm：

| 情形 | 条件 | 裁法 |
|---|---|---|
| **L 角** | 端点与邻墙端点重合 | 斜接面平分两墙：法线 = 本墙朝向接点的方向 + 邻墙背离接点的方向 |
| **T 形** | 端点落在邻墙身上（墙厚范围内、墙长范围内） | 裁到邻墙近侧墙面 |
| 共线接续 | 两墙一直线接下去 | 法线垂直于墙轴，等于不裁 |

**不同标高的墙不互相斜接**：端点平面位置一样但在不同层，说明它们本来就不相交。两墙几乎折回（夹角趋近 0）时不斜接——斜接面不稳定，会拉出一根长尖角。

## 2. 斜接面怎么落到几何上

墙的平面轮廓是一块矩形（局部 XZ：X = 墙厚，Z = 墙长，起点在 −Z 端）。斜接分两步：

1. **延长**：该端沿墙轴延长到够得着斜接面（把斜接面与两条长边的交点算出来，取最大）。不延长就补不满外角。
2. **裁剪**：用斜接面把轮廓切掉一半（半空间裁剪，Sutherland–Hodgman）。

两面墙各按同一条斜接面裁，合起来正好填满转角。裁剪时留 **1mm 咬入量**：两墙各多伸一点，交界面埋进对方体内，避免两个面正好共面闪烁。延长超过 2m（交角极锐）时放弃斜接，退回矩形。

## 3. 落在哪一层

倒角是**渲染 / 网格用的派生模型**，不是实体数据的改动：

```
WallEntity.model                  矩形轮廓 + 拉伸（真源，改参数、夹点、存盘都改这里）
  ↓ wall_render_model（bim 层，按文档里的邻墙算）
PolygonProfile（裁好的轮廓）+ 拉伸 [+ 空腔内箱 + 开口切减]
  ↓ geometry_builder（OCCT）
工作网格 / 各档 LOD
```

替换的是**基础轮廓那一步**：`RectProfile + Extrude` 换成 `PolygonProfile + Extrude`，其余特征（空心墙内箱、布尔、开口切减）按拓扑序抄过来、引用改指向新特征。所以墙的实体特征树保持原样，量厚薄长高（`wall_size`）、序列化、属性面板都不受影响。

开口切减与倒角是同一条造型路径：`wall_render_model` = 斜接轮廓 → 空腔 → 全部有效开口布尔切减。`remesh_host_openings` 也走这条路径，不会再出现「扣了洞但没倒角」的墙。

## 4. 什么时候重算

| 事件 | 入口 | 重算谁 |
|---|---|---|
| 新建墙 | `Document::add_entity` | 新墙 + 邻墙 |
| 挪墙 / 改墙参数（厚、长、高） | `Document::sync_entity_location`、`rebuild_entity_mesh` | 该墙 + 邻墙 |
| 删墙 | `Document::remove_entity` | 邻墙（斜接面没了，恢复平头） |
| 撤销 / 重做 | `Document::insert_entity` | 该墙 + 邻墙 |
| 打开 `.tdoc` | `document_io` 载入收尾 | 全部墙（旧文件里的硬拼角也变成倒角） |
| 远处 LOD | `Document::make_tess_fn`、`ensure_feature_coarse_lod` | 按同一份派生模型 |

## 5. 不做

- 墙与板 / 梁 / 柱的交接（那是别的构件的关联问题）
- 核心层、面层构造，房间边界
- 不同标高的墙互相裁剪

测试见 [`tests/wall_join_tests.cpp`](https://github.com/terry-chao/tamias/blob/main/tests/wall_join_tests.cpp)：外角是否补齐、T 形是否停在邻墙墙面、网格绕向、删墙是否恢复平头。
