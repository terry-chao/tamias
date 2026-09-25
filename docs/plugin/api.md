# 插件：API 参考

> 这一页是**手册**：插件能调用的每一个类型、成员、枚举值和命令，按名字列全。
> 想跟着做一遍，看[教程](tutorial.md)；想知道为什么边界是这样，看[设计理念](design.md)。

插件能碰到的东西只有一套：`Tamias.Api`（[`plugin-sdk/csharp/Tamias.Api/`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp/Tamias.Api)）。
`Tamias.Host` 是宿主自己的实现，插件不链接它；唯一例外是命令控制台的全局对象 `host`（见 [§2](#2)）。

契约是 C ABI（当前 **v8**），C# 这层是它的强类型包装。没有相机、GPU、Qt 句柄，也没有几何内核指针——**写路径只有 `Dispatch`**。

---

## 1. 一页速查

### 1.1 我想……用什么

| 我想做的事 | 用这个 |
|---|---|
| 知道当前有没有文档、叫什么 | `host.DocumentName` |
| 列出文档里的实体 | `host.Entities` |
| 读实体名字 / 种类 | `EntityInfo.Name` / `EntityInfo.Kind` |
| 知道用户选了谁 | `host.Selection` |
| 改选择 | `host.SetSelection(ids)` / `host.ClearSelection()` |
| 读某个实体的特征树和参数 | `host.Features(entityId)` |
| 按名字改参数 | `host.Dispatch("set_param", …)` |
| 建墙 / 建梁板柱 / 画线 | `host.Wall(…)` / `host.Beam(…)` / `host.Slab(…)` / `host.Column(…)` / `host.Line(…)`，或 `host.Dispatch("create_*", …)` |
| 让用户填尺寸 | `host.Ui.ShowForm(…)` / `host.Ui.PromptNumber(…)` |
| 在视口里点几个点 | `host.BeginPointInput(…)` |
| 在视口里点选对象 | `host.BeginEntityInput(…)` |
| 一次改一堆东西只留一步撤销 | `host.BeginTransaction(…)` |
| 往状态栏 / 控制台说话 | `host.Log("…")` |
| 加一个 Ribbon 按钮 | `host.AddCommand(…)`（只能在 `Load` 里） |
| 找到自己扩展的目录 | `ExtensionContext.SourcePath`（只在 `Load` 期间有效） |

### 1.2 类型索引

| 类型 | 形态 | 说明 | 详见 |
|---|---|---|---|
| `IHost` | 接口 | 插件唯一入口：读文档、写选择、发命令、登记按钮、开事务、起拾点 | [IHost](api/host.md) |
| `ITransaction` | 接口 : `IDisposable` | 批量编辑合成一条撤销记录；`Commit()` 才落地 | [IHost §4](api/host.md) |
| `IUi` | 接口 | 宿主 Qt 对话框：消息 / 字符串 / 数字 / 表单 / 打开 / 保存 | [宿主对话框](api/ui.md) |
| `IPlugin` | 接口 | 预编译扩展的入口：`Metadata` + `Load(IHost)` | [教程 5](tutorial/05-project-and-ship.md) |
| `PluginMetadata` | 类 | 扩展的 id / 名称 / 作者 / 版本 / 图标 / 首页 | [教程 5](tutorial/05-project-and-ship.md) |
| `RibbonPlacement` | 类 | 命令落在哪个 Ribbon page/group、顺序、图标、可选中 | [教程 5](tutorial/05-project-and-ship.md) |
| `CommandArgs` | 类 | 链式构造一条 `Dispatch` 的参数文本 | [命令与参数](api/commands.md) |
| `HostDraw` | 静态类（`IHost` 扩展方法） | 建墙 / 梁 / 板 / 柱 / 门窗 / 各种曲线的语法糖 | [命令与参数](api/commands.md) |
| `ExtensionContext` | 静态类 | `SourcePath`：当前正在加载的扩展目录 | [API 参考 §2](#2) |
| `EntityInfo` | `readonly record struct` | 一个实体的只读快照：`Id` / `Kind` / `Name` | [文档快照](api/document.md) |
| `EntityKind` | 枚举 | `Wall`…`Nurbs`，加 `Unknown` | [文档快照](api/document.md) |
| `FeatureInfo` | `readonly record struct` | 一条特征：`Id` / `Kind` / `Inputs` / `Params` | [文档快照](api/document.md) |
| `FeatureKind` | 枚举 | `RectProfile`…`Cylinder`，加 `Unknown` | [文档快照](api/document.md) |
| `FeatureParam` | `readonly record struct` | 一个特征参数：`Name` + `Value`（只有名字和 double） | [文档快照](api/document.md) |
| `PickPoint` | `readonly record struct` | 视口拾到的点：`X` / `Y` / `Z` / `EntityId` | [文档快照](api/document.md) |
| `PointInputOptions` | 类 | 拾点规格：点数上下限、吸附、预览形状、过滤 | [视口输入](api/input.md) |
| `PointInputResult` | 类 | 拾点结果：`Points` + `Cancelled` | [视口输入](api/input.md) |
| `PointInputPreviewKind` | 枚举 | 预览形状：`None` / `Curve` / `Line` / … / `Slab` | [视口输入](api/input.md) |
| `EntityInputOptions` | 类 | 拾对象规格：`MinCount` / `MaxCount` / `AllowConfirm` / `FilterKind` | [视口输入](api/input.md) |
| `EntityInputResult` | 类 | 拾对象结果：`Hits` / `EntityIds` / `Cancelled` | [视口输入](api/input.md) |
| `PromptForm` / `PromptField` | 类 | 多字段表单 | [宿主对话框](api/ui.md) |
| `PromptFieldKind` | 枚举 | `String` / `Number` / `Bool` | [宿主对话框](api/ui.md) |
| `DialogButtons` / `DialogResult` | 枚举 | 消息框的按钮与返回值 | [宿主对话框](api/ui.md) |
| `HostApi` / `HostApiVersion` | 结构体 / 静态类 | C ABI 表与版本号（给原生对照实现用） | [C ABI](api/abi.md) |

命名空间只有一个：`using Tamias.Api;`。

---

## 2. 全局对象

插件里**没有** `doc`、`document`、`selection`、`camera` 这类全局变量，也没有全局函数。
一切能力都挂在 `Load(IHost host)` 递进来的那个 `host` 上；命令回调要用，就自己在 `Load` 里捕获进闭包或存进字段。

真正的"全局"只有两个，而且都在插件之外：

| 名字 | 出现在哪 | 类型 | 说明 |
|---|---|---|---|
| `host` | **命令控制台**脚本 | `IHost` | Roslyn scripting 的 globals（[`ScriptGlobals.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/ScriptGlobals.cs)）。每段脚本自动带一个事务，所以脚本里**不要**再 `BeginTransaction`。见[脚本与命令控制台](../SCRIPTING.md) |
| `ExtensionContext.SourcePath` | **源码扩展**的 `Load` 期间 | `string` | 正在加载的扩展目录（预编译扩展是 DLL 所在目录）。`Load` 一返回就还回去了，回调里要用先存进字段 |

控制台脚本预置了 `using System;`、`System.Collections.Generic`、`System.Linq`、`Tamias.Api`，所以 `List<>`、LINQ、`host` 都不用写 using。

```csharp
// 源码扩展：Load 里先把自己目录存下来，回调里再用
static string source_ = "";

public static void Load(IHost host)
{
    source_ = ExtensionContext.SourcePath;
    host.AddCommand("my.where", "我从哪来", () => host.Log(source_));
}
```

`ExtensionContext.Enter(string)` 是宿主专用（加载扩展时进作用域、装完还原），插件不要调。

---

## 3. 命名与类型约定

| 约定 | 含义 |
|---|---|
| `ulong` id，`0` = 无 | 实体 / 特征 / 请求 id。`0` 永远不是一个合法实体 id，可以用 `id == 0` 当"没有" |
| 尺寸 / 标高都是 `double` | 模型里参数只有"名字 + 一个 double"，**没有类型和取值范围**；范围属于界面规格，不在文档里 |
| 坐标是 `float` | `PickPoint.X/Y/Z`（世界坐标，Y 向上）。`Y` 是竖直方向 |
| 字符串 UTF-8 | 中文没问题；`Dispatch` 的参数文本用 `CommandArgs` 拼，别手搓 |
| 长度单位是米 | 默认墙厚 0.2、层高 3.0 这类默认值和工具条一致 |
| 快照，不是句柄 | `Entities` / `Features` 都是一次读取的结果，不是活对象。**边改边读要重新取** |
| id 升序 / 参数名升序 | `host.Entities` 按 id 升序；`FeatureInfo.Params` 按参数名升序（顺序稳定，可以按下标遍历） |
| 依赖在前 | `FeatureInfo.Inputs` 是上游特征 id，按拓扑序排 |

---

## 4. 稳定性与版本

稳定面是 **C ABI**（[`host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)）+ [`Tamias.Api`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp/Tamias.Api) 里的公开类型。
`Tamias.Host` 的内部实现（`Host`、`PluginLoader`、`ScriptEngine`…）**不是**契约，不要反射去用。

当前 ABI 版本 **8**（`HostApiVersion.Current` 与 C++ `kHostApiVersion` 必须一致，对不上就拒绝加载）。各版本追加了什么，见 [C ABI](api/abi.md)。

演进规则：**只在表尾追加字段并升版本**，不在中间插；C# 枚举只追加、不复用旧值。

---

## 5. 下一步

- 想写第一个扩展：[教程](tutorial.md)
- 想知道插件不该做什么：[设计理念](design.md) §5、[教程 5](tutorial/05-project-and-ship.md) §6
- 想了解命令和撤销的主线：[命令与撤销](../tutorial/07-commands-and-undo.md)
