# 通用编辑操作：移动 / 复制 / 旋转 / 镜像 / 阵列

> 选中集 → 一组「目标摆放」→ **一条命令**。移动、复制、旋转、镜像、阵列都走这一条管线，
> 鼠标交互和脚本调用共用同一份实现。

上一篇：[Qt 壳](APP.md) · 相关：[命令与撤销（教程第 7 章）](tutorial/07-commands-and-undo.md)、
[关联关系](bim/relations.md)、[墙-墙交接](bim/junctions.md)、[合批 / Instancing](INSTANCING.md)、
[空间索引](SPATIAL-INDEX.md)

---

## 1. 目标与范围

从「只能看、只能建」升级到「能改」：选中几个构件，把它们搬走、复制一份、绕竖直轴转、
镜像，或者一次阵列一排 / 一圈。

设计上只有一条主线：

```
选择集(ids) ──► 算出目标摆放(Mat4) ──► 一条可撤销命令 ──► 重算交接 / 开洞 / 场景
```

- **一次操作 = 一条撤销记录**。阵列 20 份也是一步撤销，不会在用户面前留下 19 条记录。
- **交互与脚本同源**。视口里「点基点 → 点目标点」走的就是 `move_entities` / `copy_entities`
  命令；控制台里敲 `MoveEntities(ids, delta)` 是同一个命令的另一条入口。
- 命令化 undo 的规矩不变（见 [命令与撤销](tutorial/07-commands-and-undo.md)）：命令自己
  知道怎么退回去，栈不存快照。

## 2. 能力一览

| 操作 | 命令 | 交互方式 | 撤销方式 |
|---|---|---|---|
| 移动 | `move_entities` | 点基点 → 点目标点 | 写回 from 变换 |
| 复制 | `copy_entities` | 同上（生成副本） | 删掉副本（含新关系） |
| 旋转 | `rotate_entities` | 基点 → 参照方向 → 目标方向 | 写回 from 变换 |
| 镜像 | `mirror_entities` | 点镜像轴两端 | 还原快照 |
| 阵列 | `array_entities` | 对话框（线性 / 环形） | 删掉全部副本 |

## 3. 三个关键决策

### 3.1 镜像烘焙进几何，绝不写反射矩阵

`picking.cpp` 与 [空间索引 §7](SPATIAL-INDEX.md) 都写明：射线与局部坐标的换算假设场景变换是
**刚体**（平移 + 旋转，无缩放、无镜像）。把反射矩阵写进 `local_transform` 会同时坏掉拾取、
框选、BVH 假设、法线和三角绕序。

所以 `MirrorEntitiesCommand` 把镜像**烘焙进实体自身数据**：

- 有 `Location` 的族实体（墙 / 梁 / 板 / 柱 / 基础）→ 反射 `Location` 的定位点
  （`PointLocation` 的点、`LineLocation` 的两端、`SurfaceLocation` 的原点与 X 轴）；
  `Location::transform()` 会据此重建一个**右手系**摆放。
- 草图实体（线 / 折线 / 圆 / 圆弧 / 样条 / 矩形）→ 反射控制点后写回特征树，
  `local_transform` 归位。
- 盒 / 圆柱这类截面在 XZ 内对称的基础体 → 摆放取镜像后的位置与朝向。

`tests/edit_operations_tests.cpp` 里有一条断言专门守这件事：镜像后
`det(local_transform)` 仍是 **+1**（右手系），而不是 −1。

### 3.2 复制走「克隆 + 关系重映射」，网格靠内容哈希复用

复制一份构件要做四件事，缺一件就会看起来「克隆了但不对」：

1. `Entity::clone()` 深拷贝（模型 + Location + 材质引用）；
2. 新实体走 `Document::add_entity`，因此**几何相同的副本 intern 到同一个网格资产**，
   不额外占显存（见 [合批 / Instancing](INSTANCING.md)）；
3. 门窗这类宿主开口重新绑到**副本里的宿主墙**（不是原来的墙），并由
   `bind_opening_to_host` 重算沿墙参数、墙上的洞；
4. 墙复制的收尾重算墙-墙交接（`remesh_wall_neighborhood`）。

楼层归属沿用源件：副本跟着源件落在同一层，相对标高不变。

### 3.3 门窗的位置真源是「宿主墙 + 沿墙参数」，不是坐标

门 / 窗的 `local_transform` 是**结果**（`hosted_transform(墙, 摆放参数)`），不是输入。所以
移动一个门改的是 `HostedOn` 关联里的「沿墙多远 / 离地多高」，再让
`notify_entity_changed` → `reshape_hosted` 把它摆回去；移动一面墙则反向驱动它所有的开口。
这套联动在 `TransformEntitiesCommand` 里实现，撤销时把原参数还回去。

## 4. 命令参考

命令在 [`register_commands.cpp`](../src/command/core/register_commands.cpp) 注册。所有命令都接受：

- `ids`：id 数组（脚本文本协议里是 `a:ids=1|2|3`）；
- 或者单个 `entity_id`；
- 两个都没给 → 用当前选择集。

| 命令 | 参数 | 说明 |
|---|---|---|
| `move_entities` | `delta`（Vec3）或 `points`（两点） | 给参数就一步到位；都不给 → 武装交互式工具 |
| `copy_entities` | `delta` 或 `points` | 同上，生成副本并把选择移到副本上 |
| `rotate_entities` | `center`（Vec3，默认原点）+ `angle`（**度**，俯视逆时针为正） | 不给 `angle` → 三点交互（基点 / 参照 / 目标） |
| `mirror_entities` | `points`（镜像轴两端） | 不给 → 两点交互 |
| `array_entities` | `mode`=`linear`\|`polar`、`count`（**含原件**）、`spacing` / `step_angle`、`direction` / `center` | 阵列 = 算出一串摆放再复用复制路径 |

例（控制台 / 插件 / 脚本）：

```csharp
Dispatch("array_entities", "a:ids=3; s:mode=linear; i:count=5; d:spacing=6; v:direction=1,0,0");
Dispatch("rotate_entities", "a:ids=3|4; v:center=0,0,0; d:angle=90");
Dispatch("mirror_entities", "a:ids=3; p:points=0,0,0|0,0,1");
```

## 5. 界面入口

- **Ribbon → Modify**：Move / Copy / Rotate / Mirror / Array（在 Fillet / Chamfer 之前）。
- **右键菜单**（选中构件后）：Move / Copy / Rotate / Mirror / Array…
- **快捷键**：`Ctrl+M` 移动、`Ctrl+K` 复制、`Ctrl+R` 旋转、`Ctrl+Shift+M` 镜像、
  `Ctrl+Shift+A` 阵列。
- **预览**：交互过程中视口画基点→光标的引导线，并把选中构件的夹点按当前变换画成目标位置，
  所见即所得；`Esc` / 右键取消。
- **选择跟随**：复制 / 阵列完成后选择自动落到副本上（和 CAD 习惯一致）。

交互点是**水平工作面**上的点（和画墙、放柱一样），所以拖出来的是水平位移；竖直位移、
精确角度走命令参数。

## 6. 已知限制与后续

- **环形阵列的中心**目前在对话框里给（初值 = 选中范围的平面中心），还没做「点一下定圆心」。
- **斜着的镜像轴 + 特殊轮廓**：墙 / 梁 / 板 / 柱 / 草图都是精确的；空心墙那种「单侧壁厚」
  这类非对称轮廓参数暂不跟着镜像（`Location` 是精确的，轮廓参数没做镜像语义）。
- **轴网与文字注记**不在这一批范围内：它们不是 `Entity`（不进实体表），要各自补一条克隆 /
  变换路径。
- **跨文档复制粘贴**（剪贴板）未做。
- 阵列的**路径阵列**（沿折线等分）未做。

## 7. 源码锚点

| 文件 | 角色 |
|---|---|
| [src/command/edit/entity_transform.h](../src/command/edit/entity_transform.h) | 变换构造（平移 / 绕竖直轴旋转 / 镜像）、阵列摆放序列、世界摆放读写 |
| [src/command/edit/transform_entities_command.cpp](../src/command/edit/transform_entities_command.cpp) | 移动 / 旋转：改现有实体 + 门窗联动 + 交接重算 |
| [src/command/edit/copy_entities_command.cpp](../src/command/edit/copy_entities_command.cpp) | 复制 / 阵列：克隆 + 宿主关系重映射 + undo/redo |
| [src/command/edit/mirror_entities_command.cpp](../src/command/edit/mirror_entities_command.cpp) | 镜像：烘焙进几何 + 快照回滚 |
| [src/command/edit/transform_tool_command.cpp](../src/command/edit/transform_tool_command.cpp) | 交互式工具：凑齐点 → 构造上面三条命令 |
| [src/app/edit/array_dialog.cpp](../src/app/edit/array_dialog.cpp) | 阵列参数对话框 |
| [tests/edit_operations_tests.cpp](../tests/edit_operations_tests.cpp) | 阵列数学、复制带门窗、撤销/重做、镜像右手系、三点旋转 |
