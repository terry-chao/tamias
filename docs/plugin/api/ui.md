# 宿主对话框 IUi

> `Tamias.Api.IUi` —— 消息、字符串、数字、多字段表单、打开 / 保存文件。窗口一律由宿主用 Qt 弹出，外观和软件其余对话框一致。

```csharp
public interface IUi
{
    DialogResult ShowMessage(string title, string message, DialogButtons buttons = DialogButtons.Ok);
    string? PromptString(string title, string label, string defaultValue = "");
    double? PromptNumber(string title, string label, double defaultValue = 0, double? min = null, double? max = null);
    bool ShowForm(PromptForm form);
    string? OpenFile(string title, string filter);
    string? SaveFile(string title, string filter, string? defaultName = null);
}
```

拿到实例：`host.Ui`（[IHost.Ui](host.md)）。**插件不要自建 HWND / WinForms / WPF 窗口**，见[设计理念](../design.md) §5。

---

## 1. 消息框 `ShowMessage`

```csharp
DialogResult result = host.Ui.ShowMessage("摘要", $"实体 {host.Entities.Count}");
```

| 参数 | 说明 |
|---|---|
| `title` | 窗口标题 |
| `message` | 正文，可以带 `\n` 换行 |
| `buttons` | 按钮组合，默认只有一个「确定」 |

`DialogButtons` / `DialogResult`：

| `DialogButtons` | 值 | 会返回的 `DialogResult` |
|---|---:|---|
| `Ok` | 0 | `Ok` |
| `OkCancel` | 1 | `Ok` / `Cancel` |
| `YesNo` | 2 | `Yes` / `No` |
| `YesNoCancel` | 3 | `Yes` / `No` / `Cancel` |

| `DialogResult` | 值 |
|---|---:|
| `None` | 0（弹窗失败 / 宿主不可用） |
| `Ok` | 1 |
| `Cancel` | 2 |
| `Yes` | 3 |
| `No` | 4 |

```csharp
if (host.Ui.ShowMessage("删除所选", "确定删除吗？", DialogButtons.YesNo) != DialogResult.Yes)
{
    return;
}
```

---

## 2. 单字段输入

### `string? PromptString(string title, string label, string defaultValue = "")`

单行文本。**取消返回 `null`**，成功返回用户输入的文本（可能是空串）。

### `double? PromptNumber(string title, string label, double defaultValue = 0, double? min = null, double? max = null)`

数字输入。**取消返回 `null`**。`min` / `max` 都给时才限制范围（只给一个不生效）。

```csharp
if (host.Ui.PromptNumber("改深度", "深度 (m)", 3.0, 0.01, 50.0) is not double depth)
{
    return;  // 用户取消
}
```

---

## 3. 多字段表单 `PromptForm`

```csharp
var form = new PromptForm { Title = "创建墙" }
    .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
    .AddNumber("height", "高度 (m)", 3, 0.1, 50)
    .AddBool("hollow", "空心", false)
    .AddString("name", "名称", "墙-1");

if (!host.Ui.ShowForm(form))
{
    return;  // 取消
}

var thickness = form.Number("thickness");
var height = form.Number("height");
var hollow = form.Bool("hollow");
var name = form.String("name");
```

### `PromptForm`

| 成员 | 说明 |
|---|---|
| `Title` | 窗口标题（读写） |
| `Fields` | 已加字段的只读列表 |
| `AddString(id, label, value = "")` | 加一个文本框，返回 `this`（可链式） |
| `AddNumber(id, label, value, min = null, max = null)` | 加一个数字框；`min`/`max` 都给才限制范围 |
| `AddBool(id, label, value = false)` | 加一个勾选框 |
| `String(id)` | 读回文本 |
| `Number(id)` | 读回数字 |
| `Bool(id)` | 读回勾选状态 |

`ShowForm` 返回 `false` 表示取消；返回 `true` 之后字段值已经写回 `form`，用 `String`/`Number`/`Bool` 取。
`id` 必须和 `Add*` 时完全一致，否则抛 `KeyNotFoundException`。

### `PromptField`

`PromptForm` 里的一行。一般用 `Add*` + `String/Number/Bool` 就够了，这层是给需要自己遍历 `form.Fields` 的场景：

| 成员 | 说明 |
|---|---|
| `Kind` | `PromptFieldKind`：`String` / `Number` / `Bool` |
| `Id` / `Label` | 字段 id 与显示名 |
| `Text` | 文本值（`Kind == String`） |
| `Number` | 数字值（`Kind == Number`） |
| `Min` / `Max` / `HasRange` | 数字范围；`HasRange` 为 `false` 时界面上不限制 |
| `Flag` | 布尔值（`Kind == Bool`） |

| `PromptFieldKind` | 值 |
|---|---:|
| `String` | 0 |
| `Number` | 1 |
| `Bool` | 2 |

---

## 4. 文件框

### `string? OpenFile(string title, string filter)`

打开文件对话框，返回绝对路径；**取消返回 `null`**。

### `string? SaveFile(string title, string filter, string? defaultName = null)`

保存文件对话框，返回绝对路径；取消返回 `null`。`defaultName` 是预填的文件名。

`filter` 用 Qt 的写法，多个过滤器用 `;;` 分隔：

```csharp
var path = host.Ui.OpenFile("打开", "IFC (*.ifc);;所有文件 (*.*)");
var outPath = host.Ui.SaveFile("导出", "CSV (*.csv);;所有文件 (*.*)", "report.csv");
if (path is null) { return; }
```

---

## 5. 约定与坑

| 事项 | 说明 |
|---|---|
| 模态 | 这些调用是**阻塞**的，会停住 UI 线程直到用户关掉窗口；别在回调里连着弹好几个 |
| 取消 | 输入类返回 `null` / `false`，永远先判取消再算 |
| 无活动文档 | `host.Ui` 照常能用（不依赖文档） |
| 不要自建窗口 | 宿主只管自己弹出的 Qt 窗口；插件自建 HWND 不在支持范围内 |
| 大文本 | 对话框结果走的缓冲上限是 4 KB（当前值），别把整份报表塞进 `PromptString` 的默认值或表单字段里 |

相关：`BeginPointInput` / `BeginEntityInput` 用来在视口里取输入，见[视口输入](input.md)；两段式「先填尺寸、再点两点」的完整流程见[教程 4](../tutorial/04-dialogs-and-input.md)。
