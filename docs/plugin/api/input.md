# 视口输入

> 插件不订阅 Qt 鼠标事件。要取点、要取对象，就用 `BeginPointInput` / `BeginEntityInput`——鼠标、工作面、吸附、预览和取消都由宿主管。

两条路都是**非阻塞**的：调用立刻返回一个 `requestId`，用户点完或取消时宿主在 UI 线程回调。

| 方法 | 采集 | 结果类型 |
|---|---|---|
| [`host.BeginPointInput(options, callback)`](host.md) | 世界坐标点 | `PointInputResult` |
| [`host.BeginEntityInput(options, callback)`](host.md) | 点中的实体 | `EntityInputResult` |
| [`host.CancelPointInput(requestId)`](host.md) | 取消上面任一请求 | — |

---

## 1. `PointInputOptions`

| 属性 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `MinPoints` | `int` | 1 | 至少点几个才能完成 |
| `MaxPoints` | `int` | 1 | 最多点几个；**0 = 不限**，点够 `MinPoints` 后由 Enter / 双击结束 |
| `AllowConfirm` | `bool` | false | 允许 Enter / 双击提前结束（`MaxPoints = 0` 时必须开） |
| `GridSnap` | `bool` | false | 点吸到可见网格 |
| `PickEntities` | `bool` | false | 拾点时一并报告点落在哪个实体上（`PickPoint.EntityId`） |
| `EntitiesOnly` | `bool` | false | 只接受落在实体上的点；空处点击忽略（隐含 `PickEntities`） |
| `FilterKind` | `EntityKind?` | null | 只接受这个种类的实体；`null` / `Unknown` = 不过滤 |
| `WorkPlaneY` | `float` | 0 | 工作面高度：光标射线和 `y = WorkPlaneY` 求交得到点。画板时用它抬到板标高 |
| `PreviewKind` | `PointInputPreviewKind` | `None` | 拖动时预览成什么形状 |
| `PreviewCurveKind` | `string?` | null | `PreviewKind = Curve` 时细分为 `"nurbs"` / `"bspline"` / `"bezier"` |

```csharp
host.BeginPointInput(
    new PointInputOptions
    {
        MinPoints = 2,
        MaxPoints = 2,
        GridSnap = true,
        PreviewKind = PointInputPreviewKind.Wall,
    },
    result =>
    {
        if (result.Cancelled || result.Points.Count < 2) { return; }
        host.Wall(result.Points[0], result.Points[1], thickness: 0.2, height: 3);
    });
```

`PointInputPreviewKind`：

| 名字 | 值 | 名字 | 值 |
|---|---:|---|---:|
| `None` | 0 | `Circle` | 5 |
| `Curve` | 1 | `Arc` | 6 |
| `Line` | 2 | `Wall` | 7 |
| `Polyline` | 3 | `Slab` | 8 |
| `Rectangle` | 4 | | |

---

## 2. `PointInputResult`

| 成员 | 类型 | 说明 |
|---|---|---|
| `Points` | `IReadOnlyList<PickPoint>` | 采集到的点，按点选顺序。取消时是已点到的那些点（可能为空） |
| `Cancelled` | `bool` | `true` = 用户 `Esc` / 右键 / 切换文档 / 被新请求顶掉 |

**先判 `Cancelled`**，再判点数——取消时 `Points` 不保证是空的。

```csharp
if (result.Cancelled) { return; }
if (result.Points.Count < 2) { return; }
```

---

## 3. `EntityInputOptions`

| 属性 | 类型 | 默认 | 说明 |
|---|---|---|---|
| `MinCount` | `int` | 1 | 至少选几个 |
| `MaxCount` | `int` | 1 | 最多选几个；**0 = 不限**（此时自动允许 Enter 确认） |
| `AllowConfirm` | `bool` | false | 允许 Enter / 双击提前确认（`MaxCount = 0` 时自动为真） |
| `FilterKind` | `EntityKind?` | null | 只接受这个种类的实体；`null` / `Unknown` = 不过滤 |

```csharp
host.BeginEntityInput(
    new EntityInputOptions
    {
        MinCount = 1,
        MaxCount = 0,
        AllowConfirm = true,
        FilterKind = EntityKind.Wall,
    },
    result =>
    {
        if (result.Cancelled) { return; }
        host.SetSelection(result.EntityIds);
    });
```

---

## 4. `EntityInputResult`

| 成员 | 类型 | 说明 |
|---|---|---|
| `Hits` | `IReadOnlyList<PickPoint>` | 每次点击的命中点，保留重复点击与点空的记录 |
| `EntityIds` | `IReadOnlyList<ulong>` | 命中的实体 id，**去重**、丢掉 `0`，按点击顺序 |
| `Cancelled` | `bool` | 同上：取消、切文档、被新请求顶掉都是 `true` |

漏点（点在空处）不算失败，会被忽略；重复点同一个对象只算一次。

---

## 5. 生命周期与约定

| 约定 | 说明 |
|---|---|
| 一次一个 | **每个视口同时只有一个活动请求**。发起新请求会取消旧的（旧回调收到 `Cancelled = true`） |
| 什么时候被取消 | `Esc`、右键、切换文档标签、视口失效、被新请求顶掉 |
| 回调线程 | UI 线程。回调里可以安全 `Dispatch` / `SetSelection` / `Ui.ShowForm` |
| 一次性 | 回调是**一次性续延**，不是 `event`；触发过就没了。要再取点得重新 `Begin...` |
| 请求 id | `Begin*` 返回非 0 的 `requestId`；`CancelPointInput(id)` 主动取消（一样会回调 `Cancelled = true`） |
| 事务互斥 | 事务里不能 `Dispatch` 交互式命令，但可以在事务**之外**起拾点，回调里再开一个新事务 |
| 失败 | 没有活动文档 / 视口时 `BeginPointInput` 抛 `InvalidOperationException` |

为什么是"非阻塞 + 回调"而不是返回点列表：点齐的时刻由鼠标决定，不能把 C++ 的调用栈挂在那里等用户。回调是从宿主事件循环里发出来的，不在 `Dispatch` 的调用栈上。

完整的两段式流程（先 `Ui.ShowForm` 问尺寸，再拾点建墙）见[教程 4](../tutorial/04-dialogs-and-input.md)。
