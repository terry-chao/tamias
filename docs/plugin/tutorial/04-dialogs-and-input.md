# 4. 对话框与视口输入

> 插件不自建窗口，也不订阅鼠标事件。要问尺寸用 `host.Ui`，要在视口取输入用 `host.BeginPointInput` / `BeginEntityInput`——窗口、工作面、吸附、预览、取消都由宿主管。

这一章做两个命令：

- **创建墙**：弹表单填厚度/高度 → 视口点两点 → 建墙。
- **拾取对象**：视口点选若干对象 → Enter 确认 → 写进选择。

---

## 1. 先问尺寸：`PromptForm`

```csharp
var form = new PromptForm { Title = "创建墙" }
    .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
    .AddNumber("height", "高度 (m)", 3, 0.1, 50);

if (!host.Ui.ShowForm(form))
{
    return;   // 用户取消
}

var thickness = form.Number("thickness");
var height = form.Number("height");
```

要点：

- `Add*` 返回 `this`，所以能链式往下写。
- `AddNumber(id, label, value, min, max)`：**min / max 都给**才限制范围。
- `ShowForm` 返回 `false` = 取消，值没有意义；返回 `true` 之后用 `form.Number(id)` / `String(id)` / `Bool(id)` 读回。
- `id` 必须和 `Add*` 时完全一致，拼错会抛 `KeyNotFoundException`。

其他对话框：`ShowMessage`（含按钮）、`PromptString`、`PromptNumber`、`OpenFile`、`SaveFile`。取消一律返回 `null` / `false`。全表见[宿主对话框](../api/ui.md)。

---

## 2. 再取点：`BeginPointInput`

视口拾点是**非阻塞**的：调用立刻返回，用户点完后宿主在 UI 线程回调你。

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
        if (result.Cancelled || result.Points.Count < 2)
        {
            return;
        }
        host.Wall(result.Points[0], result.Points[1], thickness, height);
        host.Log("已创建墙");
    });
```

几个参数的含义：

| 选项 | 说明 |
|---|---|
| `MinPoints` / `MaxPoints` | 点数上下限；`MaxPoints = 0` 表示不限 |
| `AllowConfirm` | 允许 Enter / 双击提前结束（不限点数时必须开） |
| `GridSnap` | 吸附网格 |
| `PreviewKind` | 拖动时预览成什么形状：`Line` / `Wall` / `Slab` / `Curve` … |
| `PreviewCurveKind` | `PreviewKind = Curve` 时的细分：`"nurbs"` / `"bspline"` / `"bezier"` |
| `WorkPlaneY` | 工作面高度：光标射线和 `y = WorkPlaneY` 求交。画板时抬到板标高用 |
| `PickEntities` / `EntitiesOnly` / `FilterKind` | 点要不要带实体、要不要只认某个种类 |

**先判 `Cancelled` 再判点数**：取消时 `Points` 不保证是空的（用户可能已经点了一个点）。

---

## 3. 完整流程：创建墙

把两段拼起来，就是[示例插件](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs)里 `hello.create_wall` 的做法：

```csharp
static void BeginCreateWall(IHost host)
{
    var form = new PromptForm { Title = "创建墙" }
        .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
        .AddNumber("height", "高度 (m)", 3, 0.1, 50);
    if (!host.Ui.ShowForm(form))
    {
        return;
    }
    var thickness = form.Number("thickness");
    var height = form.Number("height");

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
            if (result.Cancelled || result.Points.Count < 2)
            {
                return;
            }
            host.Wall(result.Points[0], result.Points[1], thickness, height);
            host.Log("已创建墙");
        });
}

// 登记时只传一个函数引用
host.AddCommand("my.create_wall", "创建墙", () => BeginCreateWall(host), "填尺寸，点两点建墙");
```

为什么这样拆：`BeginPointInput` 之后函数就返回了，用户点完才回调。**别把 `Dispatch` 写在 `BeginPointInput` 后面**指望它等用户点完——那行代码会在用户点第一个点之前就执行。

`host.Wall(...)` 是 [`HostDraw`](../api/commands.md) 的扩展方法，等价于 `dispatch create_wall`；建梁/板/柱/门窗/各种曲线也有对应方法。

---

## 4. 拾取对象：`BeginEntityInput`

```csharp
host.BeginEntityInput(
    new EntityInputOptions
    {
        MinCount = 1,
        MaxCount = 0,        // 0 = 不限，可一直点
        AllowConfirm = true,
        FilterKind = EntityKind.Wall,
    },
    result =>
    {
        if (result.Cancelled || result.EntityIds.Count == 0)
        {
            return;
        }
        host.SetSelection(result.EntityIds);
        host.Log($"已选中 {result.EntityIds.Count} 个对象");
    });
```

结果里有两份数据：

| 成员 | 内容 |
|---|---|
| `Hits` | 每次点击的命中点（含重复点击、含点空） |
| `EntityIds` | 命中的实体 id，**去重**、丢掉 0，按点击顺序 |

漏点（点在空处）不算失败，会被忽略；重复点同一个对象只算一次。

---

## 5. 取消与"同时只能有一个"

这是最容易出 bug 的地方，四条约定：

| 约定 | 说明 |
|---|---|
| 一次一个 | **每个视口同时只有一个活动请求**。发起新请求会取消旧的，旧回调收到 `Cancelled = true` |
| 谁取消 | `Esc`、右键、切换文档、被新请求顶掉 |
| 一次性 | 回调是**一次性续延**，不是 `event`；触发过就没了，要再取点得重新 `Begin...` |
| 主动取消 | `BeginPointInput` 返回 `requestId`，`host.CancelPointInput(requestId)` 取消（一样会回调 `Cancelled = true`） |

所以回调里第一行永远是：

```csharp
if (result.Cancelled) { return; }
```

---

## 6. 事务与拾点的关系

事务里不能 `Dispatch` 交互式命令（点齐的时刻由鼠标决定），但可以这样组合：

```
回调外：起拾点
回调里：BeginTransaction → 若干 Dispatch → Commit
```

也就是"用户点完"之后再开事务，而不是把拾点包进事务。

---

下一章：[5. 工程、元数据与发布](05-project-and-ship.md)
