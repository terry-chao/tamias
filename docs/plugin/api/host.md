# IHost

> `Tamias.Api.IHost` —— 插件和脚本唯一的总入口。14 个成员：4 个属性、10 个方法。

```csharp
public interface IHost
{
    string DocumentName { get; }
    IReadOnlyList<EntityInfo> Entities { get; }
    IReadOnlyList<FeatureInfo> Features(ulong entityId);
    IReadOnlyList<ulong> Selection { get; }
    IUi Ui { get; }

    void Log(string message);
    void Dispatch(string command, CommandArgs? args = null);
    void AddCommand(string id, string title, Action action,
                    string? tooltip = null, RibbonPlacement? placement = null);
    void SetSelection(IEnumerable<ulong> ids);
    void ClearSelection();
    ITransaction BeginTransaction(string? name = null);
    ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> callback);
    ulong BeginEntityInput(EntityInputOptions options, Action<EntityInputResult> callback);
    void CancelPointInput(ulong requestId);
}
```

拿到 `host` 的唯一正当途径是 `IPlugin.Load(IHost)` / 源码扩展的 `static void Load(IHost)` / 控制台的全局 `host`。
不要自己 new，也不要存进 `static` 跨文档用——宿主切换文档时会重新绑定。

---

## 1. 读文档

### `string DocumentName`

当前绑定文档的名字。**没有活动文档时是空串**（停在欢迎页就是这种状态）。

```csharp
host.Log($"文档 {host.DocumentName}，实体 {host.Entities.Count}");
```

### `IReadOnlyList<EntityInfo> Entities`

文档实体表的只读快照，**按 id 升序**。每项是 `(ulong Id, EntityKind Kind, string Name)`。

要点：

- 种类名由内核给出（`"Wall"`、`"Box"`…），宿主解析成 `EntityKind`；认不出的名字是 `Unknown`。
- **不含**轴网和文字注记——它们是文档里的另外两张表，目前没有读接口。
- 每次读都重新取一遍。编辑之后旧的列表可能已经过期。
- 常配一个字典用：`var byId = host.Entities.ToDictionary(e => e.Id);`

```csharp
foreach (var entity in host.Entities)
{
    host.Log($"#{entity.Id} {entity.Kind} {entity.Name}");
}
```

字段细节见[文档快照](document.md)。

### `IReadOnlyList<FeatureInfo> Features(ulong entityId)`

某个实体的特征树（只读快照）。**实体不存在时返回空表，不抛异常**；传 `0` 同样得到空表。

每条 `FeatureInfo` 给全四件事：

| 成员 | 含义 |
|---|---|
| `Id` | 特征 id —— `set_param` / `fillet` / `chamfer` 要的就是它 |
| `Kind` | `FeatureKind`（`RectProfile`、`Extrude`、`Boolean`…） |
| `Inputs` | 上游特征 id 列表（依赖边，拓扑序，依赖在前） |
| `Params` | 参数列表，每项 `(string Name, double Value)`，**按名字升序** |

这是「窄写、宽读」里的宽读面：以前插件只能猜 `feature_id` 和参数名，现在能枚举出来，脚本编辑器的补全也有数据源。

```csharp
foreach (var feature in host.Features(entityId))
{
    host.Log($"{feature.Kind} #{feature.Id} 依赖 [{string.Join(", ", feature.Inputs)}]");
    foreach (var param in feature.Params)
    {
        host.Log($"  {param.Name} = {param.Value}");
    }
}
```

参数只有一个 `double`，**没有类型、没有取值范围**；范围是界面规格（`param_spec`），不在文档里。

### `IReadOnlyList<ulong> Selection`

当前选中的实体 id，按**文档的选择顺序**（不是升序）。没有文档或没选东西时是空表。

边删边读会看到过期数据：先 `host.Selection.ToList()` 拷一份再动。

---

## 2. 写选择

### `void SetSelection(IEnumerable<ulong> ids)`

清空当前选择，再逐个选中 `ids`，然后刷新属性面板。

- **不存在的 id 会被跳过**，不报错。
- `ids` 为空等价于 `ClearSelection()`。
- 没有活动文档时抛 `InvalidOperationException`。

### `void ClearSelection()`

清空选择，就是 `SetSelection([])`。

```csharp
host.SetSelection(host.Entities.Where(e => e.Kind == EntityKind.Wall).Select(e => e.Id));
```

---

## 3. `Log` 与 `Dispatch`

### `void Log(string message)`

UTF-8 文本，落到主窗口状态栏（约 8 秒）和**命令控制台面板**（保留最近 2000 行）。

- 写日志不产生撤销记录，也不动文档。
- 插件抛异常时宿主也会 `Log` 一条（见 [教程 5](../tutorial/05-project-and-ship.md) §5）。
- 状态栏那一行约 8 秒后消失，控制台面板里留着（最近 2000 行）。要分行就多次调用。

### `void Dispatch(string command, CommandArgs? args = null)`

把命令名 + 参数文本交给 C++ `CommandSystem`，和点工具条走同一条路：成功执行的非交互命令进撤销栈，`after_edit` 刷新视口。

- **失败抛 `InvalidOperationException`**，宿主同时把内核给的错误文本 `Log` 出来（例如 `CommandSystem: unknown command 'xxx'`、"no active document"）。
- 参数类型必须和内核读的那一种对上：核心里参数是 variant，`arg_int` **不认** `double`。id 一律用 `SetInt` / `i:`。
- 没给点列的 `create_*` 只武装工具，不执行、不进撤销栈——和点 Ribbon 按钮一样等视口喂点。
- 命令全集见[命令与参数](commands.md)。

```csharp
host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
host.Dispatch("set_param", new CommandArgs()
    .SetInt("entity_id", (long)id)
    .SetInt("feature_id", (long)featureId)
    .SetString("param_name", "depth")
    .SetDouble("value", 3.0));
```

---

## 4. 事务 `BeginTransaction`

### `ITransaction BeginTransaction(string? name = null)`

开一次批量编辑。**`Begin` 与 `Commit` 之间成功执行的命令合成一条撤销记录**——脚本改 20 个参数，用户按一次 `Ctrl+Z` 全部退回。

`name` 是这条撤销记录的显示名（撤销菜单 / 面板用），可以省略。

```csharp
public interface ITransaction : IDisposable
{
    string Name { get; }
    void Commit();
    void Abort();
    // Dispose() = Abort()
}
```

语义是**显式提交**：

| 情况 | 结果 |
|---|---|
| `Commit()` | 这一段命令合成**一条**撤销记录 |
| 不提交就 `Dispose()`（含异常从 `using` 里逃出去） | 等于 `Abort()`：逆序撤销这一段，文档回到 `Begin` 时的样子，撤销栈不留记录 |
| 显式 `Abort()` | 同上 |
| 事务里一条命令都没发 | 提交 / 回滚都不产生记录 |
| 事务里 `Dispatch` 交互式命令（点没给全） | 报错。点齐的时刻由鼠标决定，不在事务窗口里 |
| 第二次 `BeginTransaction`（嵌套） | 报错，不支持嵌套 |
| 插件命令返回时事务还开着 | 宿主回滚全部并 `Log` 一条，**不会**把用户之后的编辑吞进悬空事务 |

标准写法是 `using` + 最后 `Commit()`：

```csharp
using var tx = host.BeginTransaction("批量改参数");
foreach (var id in host.Selection.ToList())
{
    foreach (var feature in host.Features(id))
    {
        foreach (var param in feature.Params)
        {
            host.Dispatch("set_param", new CommandArgs()
                .SetInt("entity_id", (long)id)
                .SetInt("feature_id", (long)feature.Id)
                .SetString("param_name", param.Name)
                .SetDouble("value", param.Value + 0.1));
        }
    }
}
tx.Commit();  // 这一步之前，撤销栈上一个字都没留下
```

**控制台脚本例外**：每段求值宿主已经替你开了一个事务，脚本里再 `BeginTransaction` 会直接报错。

---

## 5. 登记 Ribbon 命令 `AddCommand`

### `void AddCommand(string id, string title, Action action, string? tooltip = null, RibbonPlacement? placement = null)`

在 `Load` 里登记一个 Ribbon 按钮。

| 参数 | 说明 |
|---|---|
| `id` | 命令 id，全局唯一。用 `作者或包名.动词`，例如 `mycompany.report`。重名登记失败 |
| `title` | 按钮标题（可以写中文） |
| `action` | 点击时在 UI 线程执行的回调 |
| `tooltip` | 悬停提示，可省 |
| `placement` | 落在哪、图标、顺序、是否可选中；省略就进 `home/plugins`（开始 → 插件） |

```csharp
host.AddCommand("my.report", "汇报文档",
    () => host.Log($"实体 {host.Entities.Count}"),
    "把文档规模写到状态栏");
```

约定：

- **只在 `Load` 里调用**。`Load` 时可能还没有文档，所以别在 `Load` 里 `Dispatch`——把动作放进回调。
- 命令 id 是宿主和内核共用的键，发布后保持稳定。
- 回调里抛异常不会崩进程：`Bootstrap.Invoke` 接住并 `Log` 一条。

`RibbonPlacement` 的字段和可用的 page/group id，见[教程 5](../tutorial/05-project-and-ship.md) §3。

---

## 6. 视口输入

三种调用，都是**非阻塞**的：调完立刻返回，用户点完（或取消）时宿主在 UI 线程回调你。

| 方法 | 采集什么 | 回调类型 |
|---|---|---|
| `BeginPointInput(PointInputOptions, Action<PointInputResult>)` | 世界坐标点（可带所属实体） | `PointInputResult` |
| `BeginEntityInput(EntityInputOptions, Action<EntityInputResult>)` | 点中的实体 | `EntityInputResult` |
| `CancelPointInput(ulong requestId)` | 取消指定请求（取消也走回调，`Cancelled = true`） | — |

`BeginPointInput` / `BeginEntityInput` 返回一个非 0 的 `requestId`，用于 `CancelPointInput`。

共同约定：

- **每个视口同时只能有一个活动请求**。新请求、切换文档、`Esc` 或右键都会取消旧的，并以 `Cancelled = true` 回调旧的。
- 回调发生在 UI 线程；在回调里可以安全地 `Dispatch` / `SetSelection` / `Ui.ShowForm`。
- 回调是**一次性续延**，不是 `event`：触发过一次就没了。
- 没有活动文档 / 视口不可用时 `BeginPointInput` 抛 `InvalidOperationException`。

选项与结果的完整字段见[视口输入](input.md)；两段式建墙的完整例子见[教程 4](../tutorial/04-dialogs-and-input.md)。

---

## 7. 没有活动文档时

停在欢迎页（没有打开/新建文档）时，宿主可能还没绑 `CommandSystem`。各成员的表现：

| 成员 | 无文档时 |
|---|---|
| `DocumentName` | `""` |
| `Entities` / `Selection` | 空表 |
| `Features(id)` | 空表 |
| `Log` | 正常工作 |
| `SetSelection` / `ClearSelection` | 抛 `InvalidOperationException` |
| `Dispatch` | 抛 `InvalidOperationException`（内核报 `no active document`） |
| `BeginTransaction` | 抛 `InvalidOperationException` |
| `BeginPointInput` / `BeginEntityInput` | 抛 `InvalidOperationException` |
| `Ui` | 正常工作 |
| `AddCommand` | 正常工作（`Load` 时本来就没有文档） |

这也是为什么插件命令应该先判空：

```csharp
if (host.Selection.Count == 0)
{
    host.Log("未选择对象");
    return;
}
```

---

下一篇：[文档快照与枚举](document.md) · 相关：[命令与参数](commands.md)、[视口输入](input.md)
