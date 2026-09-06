# 插件：宿主功能

> 插件能做的事 = **`IHost` 查询/写选择 + 日志 + Ribbon 命令 + 宿主视口拾点/拾对象 + 宿主对话框 + `Dispatch` 内核命令**。没有相机、GPU、自定义 Qt 句柄，也没有几何内核指针。

C# 契约在 [`plugin-sdk/csharp/Tamias.Api/`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp/Tamias.Api)。C ABI 在 [`host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)，布局必须与 [`HostApi.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Api/HostApi.cs) 一致。

每个 `IPlugin` 可通过 `Metadata` 声明稳定 id、名称、作者、是否内置、版本、发布日期、描述、首页和插件图标。未声明时，加载器使用类型名和程序集版本回退；相对图标路径按插件 DLL 目录解析。首页只接受绝对 `http/https` URL。

---

## 1. `IHost`

```csharp
public interface IHost
{
    string DocumentName { get; }
    IReadOnlyList<EntityInfo> Entities { get; }
    IReadOnlyList<ulong> Selection { get; }
    IUi Ui { get; }
    void Log(string message);
    void Dispatch(string command, CommandArgs? args = null);
    void AddCommand(string id, string title, Action action, string? tooltip = null,
                    RibbonPlacement? placement = null);
    void SetSelection(IEnumerable<ulong> ids);
    void ClearSelection();
    ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> completed);
    ulong BeginEntityInput(EntityInputOptions options, Action<EntityInputResult> completed);
    void CancelPointInput(ulong requestId);
}
```

| 成员 | 行为 |
|---|---|
| `DocumentName` | 当前绑定文档的名字；无文档时为空 |
| `Entities` | 全部实体：`Id` / `Kind` / `Name`。id 升序。种类是字符串解析成 `EntityKind`（`Wall`…`Nurbs`，解析失败为 `Unknown`） |
| `Selection` | 当前选中 id 列表（文档选择顺序） |
| `SetSelection` / `ClearSelection` | 写入选择并刷新属性面板；无效 id 会被跳过 |
| `Ui` | 宿主 Qt 对话框：消息、字符串/数字、多字段表单、打开/保存文件。窗口由 Tamias 弹出，插件不要自建 HWND |
| `Log` | UTF-8 日志；主窗口接到后显示状态栏 |
| `Dispatch` | 把命令名 + 参数文本交给 C++ `CommandSystem`；失败抛 `InvalidOperationException`（宿主会 `Log` 异常消息） |
| `AddCommand` | 在 `Load` 时登记 Ribbon 按钮。`RibbonPlacement` 可指定稳定的 page/group id、顺序、图标和可选中状态；缺省为 `home/plugins` |
| `BeginPointInput` | 非阻塞地启动宿主视口拾点；可预览线/墙/圆等。回调返回世界坐标和可选实体 id |
| `BeginEntityInput` | 只接受点中的实体（可按 `FilterKind` 过滤）；漏点忽略，重复 id 忽略 |
| `CancelPointInput` | 取消指定请求；切换文档或启动另一交互也会取消旧请求 |

`EntityInfo`：`(ulong Id, EntityKind Kind, string Name)`。  
`EntityKind`：`Unknown = -1`，其余与 C++ `EntityKind` 同序（Wall=0 … Nurbs=15）。

没有活动文档时：实体/选择为空，`Dispatch` / `SetSelection` 失败（「no active document」/ -1）。

---

## 2. 对话框 `IUi`

窗口一律由宿主用 Qt 弹出，外观跟软件其余对话框一致。

```csharp
host.Ui.ShowMessage("摘要", $"实体 {host.Entities.Count}");
if (host.Ui.PromptNumber("高度", "数值 (m)", 3, 0.1, 50) is double h) { ... }

var form = new PromptForm { Title = "创建墙" }
    .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
    .AddNumber("height", "高度 (m)", 3, 0.1, 50);
if (host.Ui.ShowForm(form)) {
    var t = form.Number("thickness");
}
var path = host.Ui.OpenFile("打开", "IFC (*.ifc);;All (*.*)");
```

`ShowMessage` 的 `DialogButtons`：`Ok` / `OkCancel` / `YesNo` / `YesNoCancel`，返回 `DialogResult`。输入类 API 取消时返回 `null` / `false`。

---

## 3. `CommandArgs` 与参数文本

C# 用链式 setter，序列化成一段文本再过 ABI：

| 方法 | 文本 |
|---|---|
| `SetInt("entity_id", 7)` | `i:entity_id=7` |
| `SetDouble("radius", 0.25)` | `d:radius=0.25` |
| `SetString("name", "wall")` | `s:name=wall` |
| `SetVec3("origin", 1, 2, 3)` | `v:origin=1,2,3` |
| `SetPoints("points", points)` | `p:points=1,2,3|4,5,6` |
| `SetDoubles("weights", weights)` | `a:weights=1|2.5` |

多参数用 `;` 拼接。内核解析见 [`parse_command_arg_text`](https://github.com/terry-chao/tamias/blob/main/src/host/command_arg_text.h)。

无类型前缀时按值推断：带逗号当 `Vec3`，纯整数当 `int64`，否则像数字当 `double`，再否则当字符串。内核读参数时按 **variant 类型**取（`arg_int` 不认 double）。**id 请用 `SetInt` / `i:`**，不要写成 `entity_id=1.0`。

---

## 4. 可 `Dispatch` 的内核命令

与工具条同一张表（[`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/register_commands.cpp)）。

### 4.1 立刻执行（适合脚本 / 插件绘制）

这些在参数给齐时 `interactive() == false`，`dispatch` 成功就会 `execute` 并压栈。

| 命令 | 主要参数 | 说明 |
|---|---|---|
| `delete_entity` | `i:entity_id` | 删一个实体 |
| `set_param` | `i:entity_id`、`i:feature_id`、`s:param_name`、`d:value` | 改特征参数并重算 |
| `fillet` / `chamfer` | `i:entity_id`、半径或距离、`i:edge` | 追加圆角 / 倒角 |
| `boolean` | `i:a`、`i:b`、`i:operation` | 布尔 |
| `set_material` | `i:entity_id` 及材质字段 | 赋材质 |
| `create_curve` | `s:curve_kind`、`p:points` | Line/Polyline/Bezier/B-spline/NURBS |
| `create_wall` | `p:points`（2 点）、`d:thickness`、`d:height` | 给齐两点则立即建墙 |
| `create_beam` | `p:points`（2 点）、`d:width`、`d:depth` | 给齐两点则立即建梁 |
| `create_slab` | `p:points`（2 对角）、`d:thickness`、`d:elevation` | 给齐两点则立即建板 |
| `create_box` / `create_cylinder` / `create_column` | `v:origin` 或 `p:points`（1 点） | 给齐原点则立即放置 |
| `create_door` / `create_window` | 同上，可选 `i:host_id` | 可贴宿主墙 |
| `create_line` / `create_polyline` / `create_circle` / `create_arc` / `create_rectangle` / `create_bezier` / `create_bspline` | `p:points` | 点数够则立即生成草图 |

C# 也可走扩展方法 [`HostDraw`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Api/HostDraw.cs)：`host.Wall(a, b, 0.2, 3)`、`host.Box(origin)`、`host.Line(a, b)`。

不给点时，上述 `create_*` 仍是交互式：dispatch 只武装工具，和 Ribbon 按钮一样要在视口点。

示例：

```csharp
host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
host.Wall(result.Points[0], result.Points[1], thickness: 0.2, height: 3);
```

未知命令名：`CommandSystem: unknown command '…'`。

`BeginPointInput` 的 `PreviewKind`：`None` / `Curve` / `Line` / `Polyline` / `Rectangle` / `Circle` / `Arc` / `Wall` / `Slab`。曲线预览仍可用 `PreviewCurveKind`（`nurbs` / `bspline` / `bezier`）。

---

## 5. C ABI（给对照实现用）

`HostApi`：`abi_version`（int32，现为 5）+ `context` + 函数指针。x64 上 int32 后有 padding，C# `LayoutKind.Sequential` 与之对齐。**只在表尾追加字段并升版本**，不要在中间插。

v5 追加：`begin_point_input` 末尾 `filter_kind`；`set_selection`；`show_dialog`。

指针约定：字符串 UTF-8；填缓冲的函数写入 `cap-1` 字节并补 `'\0'`，返回写入长度；查询失败返回 -1；`dispatch` / `register_command` / `register_plugin` / `set_selection` 成功 0、失败 -1。`show_dialog`：输入类成功 0、取消 1、失败 -1；消息框返回按钮（1=Ok, 2=Cancel, 3=Yes, 4=No）。

调用约定：Cdecl。C# 委托标了 `CallingConvention.Cdecl`；`Bootstrap.Initialize` / `Invoke` / `PointInputCompleted` 为 `[UnmanagedCallersOnly]`。视口点以 POD 数组回调，回调发生在 UI 线程。

下一篇：[开发插件](develop.md)
