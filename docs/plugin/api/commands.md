# 命令与参数

> 插件改文档只有一条路：`host.Dispatch(命令名, 参数)`。参数由 `CommandArgs` 拼成一段文本，内核解析成 variant。
> 这一页列全 `CommandArgs` 的用法和当前**所有**可 dispatch 的命令。

命令表和工具条共用同一张注册表（[`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/core/register_commands.cpp)）。
同名命令不论谁发，走的都是同一个 `Command`，所以撤销、重做、刷新都跟点按钮一样。

---

## 1. `CommandArgs`

```csharp
var args = new CommandArgs()
    .SetInt("entity_id", 7)
    .SetDouble("radius", 0.25)
    .SetString("param_name", "depth")
    .SetVec3("origin", 1, 2, 3)
    .SetPoints("points", [a, b])
    .SetDoubles("weights", [1, 2.5]);
```

| 方法 | 编码 | 例子文本 |
|---|---|---|
| `SetInt(string key, long value)` | `i:` | `i:entity_id=7` |
| `SetDouble(string key, double value)` | `d:` | `d:radius=0.25` |
| `SetString(string key, string value)` | `s:` | `s:name=wall` |
| `SetVec3(string key, float x, float y, float z)` | `v:` | `v:origin=1,2,3` |
| `SetPoints(string key, IEnumerable<PickPoint> points)` | `p:` | `p:points=1,2,3\|4,5,6` |
| `SetDoubles(string key, IEnumerable<double> values)` | `a:` | `a:weights=1\|2.5` |

多参数用 `;` 拼接（`CommandArgs` 自动做）。数字用不变区域性格式（小数点永远是 `.`），中文系统上也不会变成逗号。

### 无类型前缀时的推断

直接写 `key=value`（不带 `i:`/`d:`/…）时内核按值猜：含逗号 → `Vec3`；纯整数 → `int64`；像数字 → `double`；否则 → 字符串。

**不要依赖推断**：核心里参数是 variant，`arg_int` 不认 `double`。**id 一律 `SetInt`**；写成 `entity_id=1.0` 内核会当 double，然后取不到。

### 已知坑

| 坑 | 说明 |
|---|---|
| 字符串里的 `;` | 会被当成下一个参数的分隔符。含 `;` 的值目前没法通过 `CommandArgs` 传 |
| 值两端的空格 | 解析时会 trim，前后空格会丢 |
| 字符串里的 `=` | 没事：内核按第一个 `=` 分割，后面的 `=` 属于值 |
| 空字符串 | `s:name=` 是合法的空串 |

---

## 2. 交互式还是一步到位

同一条命令有两种形态，取决于你给没给"点"：

| 形态 | 什么时候 | 效果 |
|---|---|---|
| 立即执行 | 参数给齐（`p:points` 点数够，或给了 `v:origin`） | `Dispatch` 成功就立刻执行并进撤销栈 |
| 武装工具 | 点没给齐 | 只把工具架起来，和点 Ribbon 按钮一样等用户在视口点；**不进撤销栈** |

所以脚本里要把点喂全，否则用户会看到"命令好像没反应"。

几个通用参数名：

| 参数 | 含义 |
|---|---|
| `entity_id` | 单个目标实体 |
| `ids` | 多个目标（`a:` 数组）。编辑类命令里 `ids` 优先于 `entity_id`，都没有就用当前选择集 |
| `points` | 点数组（`p:`）。创建类命令用它摆位 |
| `origin` | 单点（`v:`），等价于只有一个点的 `points` |

---

## 3. 文档与特征

| 命令 | 参数 | 说明 |
|---|---|---|
| `delete_entity` | `entity_id` | 删一个实体 |
| `set_param` | `entity_id`、`feature_id`、`param_name`(`s`)、`value`(`d`) | 改特征参数并重算几何 |
| `fillet` | `entity_id`、`radius`(`d`，默认 0.1)、`edge`(`i`，默认 0) | 追加圆角特征 |
| `chamfer` | `entity_id`、`distance`(`d`，默认 0.1)、`edge`(`i`，默认 0) | 追加倒角特征 |
| `boolean` | `a`(`i`)、`b`(`i`)、`operation`(`i`：0=Fuse / 1=Common / 2=Cut) | 布尔运算 |
| `set_material` | 见 [§7](#7) | 赋材质 |
| `create_storey` | `name`(`s`，默认 `Storey`)、`elevation`(`d`，默认 0) | 新建楼层 |
| `set_location` | `entity_id`、`storey_id`(`i`)、`elevation_offset`(`d`) | 把实体放到某楼层 |

```csharp
host.Dispatch("set_param", new CommandArgs()
    .SetInt("entity_id", (long)entityId)
    .SetInt("feature_id", (long)featureId)
    .SetString("param_name", "depth")
    .SetDouble("value", 2.5));
```

---

## 4. 创建：BIM 构件

给齐点就立即建（`create_beam` 的 T/I 断面目前只支持交互式）：

| 命令 | 参数 |
|---|---|
| `create_wall` | `points`（2 点）、`thickness`(`d`，0.2)、`height`(`d`，3.0)、`leaf`(`d`，>0 = 空心墙) |
| `create_structural_wall` | `points`（2 点）、`thickness`(0.3)、`height`(3.0) |
| `create_curtain_wall` | `points`（2 点）、`thickness`(0.15)、`height`(3.0) |
| `create_beam` | `points`（2 点）、`width`(0.3)、`depth`(0.5)；`sub_type`(`s`) 为 `tee`/`i` 时改用 `flange_width`/`web_thickness`/`height`/`flange_thickness`，且只支持交互式 |
| `create_slab` | `points`（2 对角点）、`thickness`(0.2)、`elevation`(`d`，默认当前楼层层高 / 本层顶；无楼层时为 0) |
| `create_column` | `points`（1 点）或 `origin`、`sub_type`(`s`：`rect`/`circle`)、`width`(0.4)、`depth`(0.4)、`diameter`(0.4)、`height`(3.0)、`host_id`(`i`) |
| `create_foundation` | `points`（1 点）或 `origin`、`sub_type`(`s`：`isolated`/`strip`/`raft`/`pile`)、`length`、`width`、`height`、`diameter` |
| `create_door` | `points`（1 点）或 `origin`、`width`(1.0)、`height`(2.1)、`thickness`(0.05)、`sill`(0.0)、`host_id`(`i`，可贴宿主墙) |
| `create_window` | `points`（1 点）或 `origin`、`width`(1.2)、`height`(1.2)、`thickness`(0.08)、`sill`(0.9)、`host_id`(`i`) |

---

## 5. 创建：草图与曲线

| 命令 | 参数 | 说明 |
|---|---|---|
| `create_line` | `points`（2 点） | 直线 |
| `create_polyline` | `points`（≥2） | 折线 |
| `create_circle` | `points`（2 点：圆心 + 半径点） | 圆 |
| `create_arc` | `points`（3 点：起点 / 经过点 / 终点） | 圆弧 |
| `create_rectangle` | `points`（2 对角点） | 矩形 |
| `create_bezier` | `points` | 贝塞尔 |
| `create_bspline` | `points` | B 样条 |
| `create_curve` | `curve_kind`(`s`：`line`/`polyline`/`bezier`/`bspline`/`nurbs`)、`points`、`weights`(`a`，可选)、`degree`(`i`，可选) | 通用曲线 |

```csharp
host.Dispatch("create_curve", new CommandArgs()
    .SetString("curve_kind", "nurbs")
    .SetPoints("points", result.Points)
    .SetDoubles("weights", [1, 1, 1]));
```

---

## 6. 通用编辑

移动 / 复制 / 旋转 / 镜像 / 阵列都按「目标集 → 一组摆放 → 一条可撤销命令」走。目标集取 `ids` → `entity_id` → 当前选择集。

| 命令 | 参数 | 说明 |
|---|---|---|
| `move_entities` | `ids`/`entity_id`，加 `delta`(`v`) 或 `points`（2 点） | 平移；都没给就变成交互式移动工具 |
| `copy_entities` | 同上 | 复制 |
| `rotate_entities` | `ids`/`entity_id`、`angle`(`d`，**度**)、`center`(`v`，默认原点) | 绕 Y 轴转 |
| `mirror_entities` | `ids`/`entity_id`、`points`（2 点 = 镜面线） | 镜像 |
| `array_entities` | `ids`/`entity_id`、`mode`(`s`：`linear`/`polar`)、`count`(`i`，默认 3，**含原件**) | 阵列 |

`array_entities` 的分支参数：

| `mode` | 参数 |
|---|---|
| `linear` | `direction`(`v`，默认 `1,0,0`)、`spacing`(`d`，默认 1.0) |
| `polar` | `center`(`v`)、`step_angle`(`d`，每份夹角，度)；不给就按 `total_angle`(`d`，默认 360) 和 `count` 推 |

```csharp
host.Dispatch("move_entities", new CommandArgs()
    .SetDoubles("ids", host.Selection.Select(id => (double)id))
    .SetVec3("delta", 0, 0, 5));
```

```csharp
host.Dispatch("array_entities", new CommandArgs()
    .SetInt("entity_id", (long)id)
    .SetString("mode", "linear")
    .SetVec3("direction", 1, 0, 0)
    .SetDouble("spacing", 3.0)
    .SetInt("count", 5));
```

---

## 7. 材质

`set_material` 参数：`entity_id`、`material_id`(`i`)、`name`(`s`)、`base_color`(`v`)、`roughness`(0.6)、`metallic`(0.0)、`opacity`(1.0)、`albedo_texture_id`/`normal_texture_id`/`orm_texture_id`(`i`)、`tex_scale_x`/`tex_scale_y`、`tex_offset_x`/`tex_offset_y`、`tex_rotation`、`tex_world_scale`(2.0)。

材质**只能写不能读**——`host.Entities` 里没有材质字段。

---

## 8. 轴网与文字

| 命令 | 参数 | 说明 |
|---|---|---|
| `create_grid_axis` | `name`(`s`)、`direction`(`s`：`x`/`z`)、`position`(`d`)、`start`(`d`)、`end`(`d`) | 单根轴线 |
| `auto_grid` | `origin_x`、`origin_z`、`x_spacings`(`a`)、`z_spacings`(`a`)、`margin`(`d`，默认 1.0) | 按间距表一次生成正交轴网 |
| `delete_grid_axis` | `axis_id`(`i`) | 删轴 |
| `create_text` | `kind`(`s`：`annotation`/`axis`/`storey`/`dimension`/`room`/`title`/`drawing`)、`text`(`s`)、`position`(`v`)、`size_px`(`d`，14)、`color`(`v`)、`opacity`(`d`)、`align`(`s`：`left`/`center`/`right`) | 世界锚点 + 屏幕朝向的文字 |
| `update_text` | `text_id`(`i`) + 上面除 `kind` 外的任意字段 | **只改传进来的字段**，没给保持原值 |
| `delete_text` | `text_id`(`i`) | 删文字 |

轴网和文字注记**不在** `host.Entities` 里，所以插件拿不到它们的 id 列表，只能自己记住创建时的情况。

---

## 9. `HostDraw` 扩展方法

创建类命令的语法糖，省掉手搓 `CommandArgs`。`using Tamias.Api;` 之后直接挂在 `host` 上：

| 方法 | 等价命令 |
|---|---|
| `host.Wall(start, end, thickness = 0.2, height = 3.0)` | `create_wall` |
| `host.Beam(start, end, width = 0.3, depth = 0.5)` | `create_beam` |
| `host.Slab(a, b, thickness = 0.2, elevation = null)` | `create_slab` |
| `host.Column(origin)` | `create_column` |
| `host.Door(origin, hostId = 0)` | `create_door` |
| `host.Window(origin, hostId = 0)` | `create_window` |
| `host.Line(a, b)` | `create_line` |
| `host.Polyline(points)` | `create_polyline` |
| `host.Circle(center, radiusPoint)` | `create_circle` |
| `host.Arc(start, through, end)` | `create_arc` |
| `host.Rectangle(a, b)` | `create_rectangle` |
| `host.Bezier(points)` | `create_bezier` |
| `host.BSpline(points)` | `create_bspline` |

```csharp
host.Wall(result.Points[0], result.Points[1], thickness: 0.2, height: 3);
```

`HostDraw` 没有覆盖到的（门窗、文字、布尔……）直接发命令，`CommandArgs` 一样短：

```csharp
host.Dispatch("create_window", new CommandArgs()
    .SetPoints("points", [origin])
    .SetInt("host_id", (long)wallId));
```

```csharp
host.Dispatch("create_text", new CommandArgs()
    .SetString("kind", "annotation")
    .SetString("text", "hello")
    .SetVec3("position", 0, 0, 0));
```

注意 `EntityKind` 里虽然有 `Box` 和 `Cylinder`，但当前**没有**创建它们的命令，`HostDraw` 也没有对应方法——柱 / 圆柱用 `create_column`（`sub_type` 选 `rect` / `circle`）。

---

## 10. 失败时

| 情况 | 表现 |
|---|---|
| 命令名不存在 | `Dispatch` 抛 `InvalidOperationException`；状态栏 / 控制台出现 `CommandSystem: unknown command '…'` |
| 参数类型不对（例如 id 给了 double） | 命令多半**静默用默认值**——内核按 variant 取参数，类型不匹配就当没给 |
| 实体已删 / id 不存在 | 多数命令用 `0` 或空对象兜底，个别会报错 |
| 没有活动文档 | 抛 `InvalidOperationException`（`no active document`） |
| 交互式命令被塞进事务 | 抛错：点齐的时刻由鼠标决定，不在事务窗口里 |
