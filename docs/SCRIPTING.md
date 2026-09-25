# 脚本与命令控制台

> 一个面板，两件事：**输出**——每条执行过的内核命令长一行可抄走的 C# 调用；
> **输入**——敲一段 C# 跑，`host` 就是当前文档。它和插件是同一个宿主（`IHost`），
> 区别只在生命周期：插件是装好的扩展，脚本是随手写的文件。

代码：[`console_panel`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/console_panel.cpp)（面板）、
[`code_editor`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/code_editor.cpp)（带行号的编辑器）、
[`script_store`](https://github.com/terry-chao/tamias/blob/main/src/app/base/script_store.h)（脚本目录）、
[`command_echo.h`](https://github.com/terry-chao/tamias/blob/main/src/host/command_echo.h)（回显文本）、
[`ScriptEngine.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/ScriptEngine.cs)（求值）。

---

## 1. 打开

**视图 → 面板 → 命令控制台**，或 `Ctrl+Shift+J`。底部停靠，默认收起。输出和编辑器之间
是可拖动的分隔条。

前提：装了 .NET 运行时（和插件同一个前提）。没有时面板只回显、不能求值，其余功能照常。

## 2. 输出面：点着学 API

在工具条上拉一面墙，面板里就出现：

```csharp
host.Dispatch("create_wall", new CommandArgs()
    .SetDouble("height", 3)
    .SetPoints("points", [new PickPoint(0f, 0f, 0f, 0), new PickPoint(5f, 0f, 0f, 0)])
    .SetDouble("thickness", 0.2));
```

那面墙的参数和坐标都在里面。抄走、改个厚度、粘回编辑器里跑，就是一面新墙。

几条实现约定：

- **文本由宿主生成，内核不知道回显长什么样。** `CommandSystem` 只在命令**真正执行**时
  回调一次（[`CommandObserver`](https://github.com/terry-chao/tamias/blob/main/src/command/core/command_system.h)），
  `Session` 把它渲染成 C#（[`format_dispatch_call`](https://github.com/terry-chao/tamias/blob/main/src/host/command_echo.cpp)）。
  想换格式（比如改成 Rust / 命令文本）只动这一个函数。
- **参数按名字排序。** `CommandArgs` 是 `unordered_map`，不排序的话同一件事每次回显的顺序都不一样。
- **武装工具不算执行。** `dispatch` 一个没给点的 `create_wall` 只是把工具架起来，不产生回显；
  用户点完两点、命令真正执行了，才长出一行——而且带的是**补齐了点的等价一次性调用**。
  这一条靠 `Command::echo_args()`：交互式命令把采集到的点报上来，和武装参数合并。
- **插件 / 脚本的输出落在同一个面板**：`host.Log(...)` 和命令回显走同一个地方，只是来源不同。
- 面板只留最近 2000 行，旧行自动淘汰。

## 3. 输入面：一次求值 = 一个事务

```
ConsolePanel（Ctrl+Enter）
  → MainWindow
    → PluginHost::evaluate
      → CSharpRuntime（hostfxr 的函数指针）
        → Tamias.Host.Bootstrap.Evaluate
          → ScriptEngine（Roslyn scripting + InteractiveAssemblyLoader）
            → 脚本里每个 host.Dispatch 走的还是那条命令主线
```

```csharp
// 面板里直接敲这个
foreach (var id in host.Selection.ToList())
    foreach (var f in host.Features(id))
        host.Log($"{f.Kind} {string.Join(", ", f.Params.Select(p => $"{p.Name}={p.Value}"))}");
```

- **`host` 就是 `IHost`**（globals）。文档、示例插件里怎么写，这里就怎么写；
  可用的成员见[宿主功能](plugin/api.md)。
- **最后那个表达式的值会回来。** 敲 `host.Selection.Count` 回一行 `1`；敲 `host.Entities`
  格式化成一行列表，而不是类型名。
- **每段自带一个事务**（ABI v7）。写到一半发现不对，按一次 `Ctrl+Z` 全部退回——
  所以**不要**在脚本里自己再 `BeginTransaction`，不支持嵌套，会直接报错。
- **报错会把光标带到出错那行。** Roslyn 的诊断是 `(行,列): error CSxxxx: …`，面板解析出来
  直接跳过去。
- 求值**同步跑在 UI 线程**上：第一次会等 Roslyn 预热，长脚本会冻界面。

## 4. 脚本文件

脚本住在 **`<AppData>/scripts`**（Windows 上大致 `%APPDATA%/tamias/tamias/scripts`）。

| 动作 | 行为 |
|---|---|
| `Ctrl+S` / 保存 | 还没落过盘就直接存进脚本目录（`script1.cs`…），不弹框；落过盘就覆盖 |
| 另存为… | 弹文件框，默认给脚本目录 |
| New / 切换下拉里的脚本 | 有未保存改动先问一句（标题上带 `*`） |
| 上次编辑的脚本 | 记在 `QSettings` 里，下次打开接着编 |
| 起手式 | 没有历史时给一段能立刻跑的示例。**代码不翻译**——翻译代码只会让它不再是能跑的那段 |

**脚本不进 `.tdoc`。** 工作文档是数据，脚本是行为；混进去会把[路线图](ROADMAP.md)里
「工作格式 vs 交换格式」那层分层糊掉。放成文件还有个好处：能用 git 管，能拷给同事。

## 5. 安全模型：全信任，不是沙箱

脚本在 Tamias 进程里跑，拿到的是 `IHost` 的**全部**能力——读文档、发命令、弹宿主对话框。
没有任何权限、沙箱或超时。

这是有意的，和 FreeCAD 的 Python 控制台同性质：**它是给操作者自己用的工具，不是安全边界。**
第 5 节的「全信任」意味着：别人给你的 `.cs`，运行它就等于运行一个 exe。

## 6. 和插件什么关系

同一份 `IHost`、同一套命令、同一套事务；差在生命周期：

| | 插件 | 脚本 |
|---|---|---|
| 装在哪 | `<exe>/plugins/*.dll` | `<AppData>/scripts/*.cs` |
| 何时加载 | 启动时扫一遍，重启生效 | 按需求值，改完立刻能跑 |
| 元数据 | 有 id / 版本 / 作者 / 图标 | 没有 |
| 入口 | `IPlugin.Load(IHost)` 里 `AddCommand` | 直接是一段代码 |
| 进 Ribbon | 能（`RibbonPlacement`） | 不能（还没有「钉成按钮」） |
| 事务 | 手动 `BeginTransaction` | 每段求值自动一个 |

写得顺手的脚本，迟早会想变成插件——那时候把代码搬进 `IPlugin.Load` 即可，API 是同一套。

## 7. 改这里要先知道的两个坑

1. **Roslyn 的每一次求值都编译成独立程序集。** 后果两条：globals 类型和字段**必须是
   `public`**（否则 CS0122）；宿主那两份程序集（`Tamias.Host` / `Tamias.Api`）必须用
   `InteractiveAssemblyLoader.RegisterDependency` 登记，否则脚本加载器会在自己的
   ALC 里再加载一份，报 `cannot be cast to ...`——两个同名类型互不认。
   `Tamias.Host` 是 hostfxr 装进**独立 ALC** 的，所以这不是假想问题。
2. **C# 源码要重新 publish 才会生效。** 拉进 `tamias` 目标的 `dotnet publish` 靠
   [`cmake/TamiasDotnet.cmake`](https://github.com/terry-chao/tamias/blob/main/cmake/TamiasDotnet.cmake)
   的源码 glob 触发，那个 glob 带 `CONFIGURE_DEPENDS`——新加的 `.cs` 才不会被漏掉。
   改完 C# 只 rebuild 是没用的，要 build `tamias` 目标。

## 8. 现在还没有

| 缺什么 | 说明 |
|---|---|
| **补全** | 数据早就有（[`IHost.Features`](plugin/api.md) 能枚举特征和参数），缺的是把 Roslyn 的 completion 接到编辑器上。现在仍得先读文档才知道 `host.` 后面有什么 |
| 多页签 | 一次只能开一个脚本 |
| 断点 / 单步 | 没有调试器，靠 `host.Log` |
| 长脚本不冻界面 | 求值是同步的；以后要么搬到线程，要么给取消/进度 |
| 脚本钉成 Ribbon 按钮 | 插件能做，脚本还不能 |

上一页：[插件](plugin/index.md) ·
相关：[Qt 客户端](APP.md)（面板在哪）、[命令与撤销](tutorial/07-commands-and-undo.md)（命令主线）
