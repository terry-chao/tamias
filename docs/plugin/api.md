# 插件：宿主功能

> 插件能做的事 = **`IHost` 只读查询 + 日志 + Ribbon 命令 + 宿主视口拾点 + `Dispatch` 内核命令**。没有相机、文件对话框、自定义 Qt 面板、也没有几何内核句柄。

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
    void Log(string message);
    void Dispatch(string command, CommandArgs? args = null);
    void AddCommand(string id, string title, Action action, string? tooltip = null,
                    RibbonPlacement? placement = null);
    ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> completed);
    void CancelPointInput(ulong requestId);
}
```

| 成员 | 行为 |
|---|---|
| `DocumentName` | 当前绑定文档的名字；无文档时为空 |
| `Entities` | 全部实体：`Id` / `Kind` / `Name`。id 升序。种类是字符串解析成 `EntityKind`（`Wall`…`Nurbs`，解析失败为 `Unknown`） |
| `Selection` | 当前选中 id 列表（文档选择顺序） |
| `Log` | UTF-8 日志；主窗口接到后显示状态栏 |
| `Dispatch` | 把命令名 + 参数文本交给 C++ `CommandSystem`；失败抛 `InvalidOperationException`（宿主会 `Log` 异常消息） |
| `AddCommand` | 在 `Load` 时登记 Ribbon 按钮。`RibbonPlacement` 可指定稳定的 page/group id、顺序、图标和可选中状态；缺省为 `plugins/commands` |
| `BeginPointInput` | 非阻塞地启动宿主视口拾点；回调返回世界坐标和可选实体 id。每个视口同时只有一个请求 |
| `CancelPointInput` | 取消指定请求；切换文档或启动另一交互也会取消旧请求 |

`EntityInfo`：`(ulong Id, EntityKind Kind, string Name)`。  
`EntityKind`：`Unknown = -1`，其余与 C++ `EntityKind` 同序（Wall=0 … Nurbs=15）。

没有活动文档时：实体/选择为空，`Dispatch` 失败（「no active document」）。

---

## 2. `CommandArgs` 与参数文本

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

## 3. 可 `Dispatch` 的内核命令

与工具条同一张表（[`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/register_commands.cpp)）。

### 3.1 立刻执行（适合脚本）

这些 `interactive() == false`，`dispatch` 成功就会 `execute` 并压栈，视口随后 `refresh_after_edit`。

| 命令 | 主要参数 | 说明 |
|---|---|---|
| `delete_entity` | `i:entity_id` | 删一个实体 |
| `set_param` | `i:entity_id`、`i:feature_id`、`s:param_name`、`d:value` | 改特征参数并重算 |
| `fillet` | `i:entity_id`、`d:radius`（默认 0.1）、`i:edge` | 追加圆角特征 |
| `chamfer` | `i:entity_id`、`d:distance`（默认 0.1）、`i:edge` | 追加倒角特征 |
| `boolean` | `i:a`、`i:b`、`i:operation` | 布尔；`operation` 与 `BooleanOp` 整型一致 |
| `set_material` | `i:entity_id`，以及 `i:material_id`、`s:name`、`v:base_color`、`d:roughness`、`d:metallic`、贴图 id 等 | 赋材质 |
| `create_curve` | `s:curve_kind`、`p:points`、可选 `a:weights`、`i:degree` | 从完整定义创建 Line/Polyline/Bezier/B-spline/NURBS |

示例：

```csharp
host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
host.Dispatch("set_param", new CommandArgs()
    .SetInt("entity_id", (long)id)
    .SetInt("feature_id", 1)
    .SetString("param_name", "height")
    .SetDouble("value", 3.2));
```

### 3.2 交互式（dispatch 只「武装」工具）

`create_wall` / `create_beam` / `create_box` / `create_cylinder` / `create_column` / `create_slab` / `create_door` / `create_window` 以及草图类 `create_line`、`create_polyline`、`create_circle`、`create_arc`、`create_bezier`、`create_rectangle`、`create_bspline` 在 C++ 里仍是交互命令。新插件应优先用 `BeginPointInput` 收集输入，再 dispatch 非交互文档命令。

部分创建命令接受默认尺寸，但仍然要拾取：

| 命令 | 可选参数 |
|---|---|
| `create_wall` | `d:thickness`、`d:height` |
| `create_beam` | `d:width`、`d:depth` |
| `create_slab` | `d:thickness`、`d:elevation` |

`BeginPointInput` 由宿主处理工作面、网格吸附、实体拾取、Enter/双击确认和 Esc/右键取消；C# 不接触 Qt 事件。NURBS 示例见 `plugins/csharp/Tamias.Nurbs`。

未知命令名：`CommandSystem: unknown command '…'`。

---

## 4. C ABI（给对照实现用）

`HostApi`：`abi_version`（int32，现为 4）+ `context` + 函数指针。x64 上 int32 后有 padding，C# `LayoutKind.Sequential` 与之对齐。

指针约定：字符串 UTF-8；填缓冲的函数写入 `cap-1` 字节并补 `'\0'`，返回写入长度；查询失败返回 -1；`dispatch` / `register_command` / `register_plugin` 成功 0、失败 -1。`register_plugin` 在 `Load` 每个 `IPlugin` 之前提交完整 metadata；之后的 `register_command` 记在该插件名下。

调用约定：Cdecl（Win x64 实际只有一种）。C# 委托标了 `CallingConvention.Cdecl`；`Bootstrap.Initialize` / `Invoke` / `PointInputCompleted` 为 `[UnmanagedCallersOnly]`。视口点以 POD 数组回调，回调发生在 UI 线程。

下一篇：[开发插件](develop.md)
