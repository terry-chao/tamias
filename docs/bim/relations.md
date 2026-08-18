# 关联关系

> 墙和窗是关联的。墙动了之后，通过关联关系找到对应的窗，通知到窗，重新造型；造型之后对齐，进行合法性检查，然后结束。关系本身进 `.tdoc`。

这是 [BIM 业务层](../BIM.md) 里已经落地的那一块。楼层、轴网还没有；宿主图是另一张表，不替代 `SceneNode.parent`。

代码在 [`src/bim/`](https://github.com/terry-chao/tamias/tree/main/src/bim)。

---

## 1. 它管什么

显式关系，不靠坐标反推。

| | |
|---|---|
| **谁连谁** | 窗 / 门（`from`）`HostedOn` 墙（`to`） |
| **开口在哪** | `along` / `sill` / `offset`，相对墙局部 |
| **墙一改** | 查出从属开口 → 通知 → 重造型 → 对齐 → 合法性检查 → 结束 |
| **存哪** | `Document::bim()` 关系表，`.tdoc` 的 `RELA` chunk |

**不管：** 墙怎么挤成实体（特征树）、世界矩阵怎么乘（语义树）、开洞布尔、墙连接裁剪。删墙时只清关系，开口留在原地。

---

## 2. 数据

```
Relation
  id          关系自己的句柄（.tdoc 里存，和构件句柄一样能在 Ctrl+D 里看到）
  kind        目前只有 HostedOn
  from        从属构件（窗、门）
  to          宿主构件（墙）
  placement   along / sill / offset
  valid       对齐之后是否仍完全落在墙内
```

`BimModel` 持有这张表，和 `Scene`、实体表并列。删实体时 `remove_involving` 清掉相关关系。

开口在墙上的参数（墙局部：X = 厚，Y = 高，Z = 长）：

| 字段 | 含义 |
|---|---|
| `along` | 沿墙长，0 = 起点，1 = 终点 |
| `sill` | 距墙底的高度（门固定为 0） |
| `offset` | 沿墙厚，0 = 墙中心 |

窗/门的局部 X 是宽，墙的局部 Z 是长，放置时绕 Y 转 −90° 对齐。

一类一文件：

```
src/bim/relation_kind.h     // RelationKind
src/bim/host_placement.h    // HostPlacement
src/bim/relation.h          // Relation
src/bim/bim_model.h         // 关系表
src/bim/host_geometry.h     // 墙框、开口尺寸、对齐、合法性
src/bim/host_update.h       // notify / bind
```

---

## 3. 更新管线

```
墙参数改了（SetFeatureParamCommand）
  → notify_entity_changed(wall)
      → 按 to == wall 查出 HostedOn 的窗/门
      → 通知每个开口：
          1. 重新造型：开口厚度跟上墙厚，createGeom 重算网格
          2. 对齐：along / sill 夹进墙的可用范围
          3. 合法性检查：开口是否仍完全落在墙长、墙高内
      → 结束
```

放置：窗/门工具点在墙上（射线命中墙）→ `bind_opening_to_host` → 写入关系并立刻走同一套管线。点在空地上不建关系，开口保持独立放置。

实现：[`host_update.cpp`](https://github.com/terry-chao/tamias/blob/main/src/bim/host_update.cpp)、[`host_geometry.cpp`](https://github.com/terry-chao/tamias/blob/main/src/bim/host_geometry.cpp)。

命令接缝：

```
CreatePrimitiveCommand（窗/门）
  → Document::add_entity(...)
  → bind_opening_to_host(window, wall, hit)   // 点中墙时

SetFeatureParamCommand
  → 改墙特征参数、重算墙网格
  → notify_entity_changed(wall)
```

---

## 4. 进 .tdoc

`.tdoc` 格式版本 **6**。新增 chunk `RELA`：

```
next_relation_id : u64
count            : u64
[ id, kind, from, to, along, sill, offset, valid ] × count
```

版本 5 的文件仍能打开（没有 `RELA` 就是空表）。内存快照（undo 用的 `serialize_document`）同样带上关系表。

---

## 5. 怎么看

**Ctrl+D** 打开句柄检查窗口，点选构件：显示 `.tdoc` 里的句柄，以及该构件上的 `HostedOn` 关系（沿墙参数、是否合法）。这是 app 调试工具，规则仍在 `src/bim`，不在 Qt 里。

---

## 附录

- [BIM 业务层](../BIM.md) —— 这一层还管楼层 / 轴网（尚未实现）
- [create_primitive_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/create_primitive_command.cpp) —— 点中墙时绑宿主
- [set_feature_param_command.cpp](https://github.com/terry-chao/tamias/blob/main/src/command/set_feature_param_command.cpp) —— 改参后通知从属
- [handle_inspector.cpp](https://github.com/terry-chao/tamias/blob/main/src/app/handle_inspector.cpp) —— Ctrl+D
