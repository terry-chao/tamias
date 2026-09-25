# 插件：教程

> 从"一个能点的按钮"开始，五章写完一个会读文档、会改文档、会在视口里取输入的 C# 扩展。
> 查 API 用[API 参考](api.md)；想先搞清楚为什么只能这么写，看[设计理念](design.md)。

---

## 1. 先选一条路

两条路进的是同一套东西：同一份 `IHost`、同一张扩展清单、同一个插件管理界面。

| | 源码扩展 | 预编译扩展 |
|---|---|---|
| 形态 | 一个 `main.cs`（+ 可选 `extension.json`） | 一个 class library（`.dll`） |
| 入口 | 任意类型上的 `public static void Load(IHost)` | 实现 `IPlugin` 的 public 类 |
| 要工程吗 | 不要 | 要 |
| 加载方式 | 启动 / 保存时用 Roslyn 现编译 | hostfxr 加载程序集 |
| 改完生效 | **保存即生效**（自动重载） | 重新 publish + 重启 |
| 元数据 | 来自 `extension.json` | 来自 `PluginMetadata` |
| 适合 | 小工具、试想法、团队内部脚本 | 要发布、要第三方依赖、要图标版本号 |

不确定就走源码扩展——写完发现合适了，再搬进[第 5 章](tutorial/05-project-and-ship.md)的工程里，代码基本能原样粘过去。

**共同前提**：装了 .NET 8（或更新）运行时。没有时主程序照常开，只是没有插件命令。

---

## 2. 章节

| 章 | 你会做出什么 | 用到的 API |
|---|---|---|
| [1. 第一个扩展](tutorial/01-first-extension.md) | 一个能点的 Ribbon 按钮，点一下打一行日志 | `AddCommand`、`Log` |
| [2. 读文档](tutorial/02-read-document.md) | "列出选择"和"列出特征"两个命令 | `DocumentName`、`Entities`、`Selection`、`Features` |
| [3. 改文档](tutorial/03-edit-and-undo.md) | 删所选、批量改参数，整批只占一步撤销 | `Dispatch`、`CommandArgs`、`BeginTransaction` |
| [4. 对话框与视口输入](tutorial/04-dialogs-and-input.md) | 填尺寸 → 视口点两点 → 建一面墙；再做一个拾取对象 | `IUi`、`BeginPointInput`、`BeginEntityInput`、`HostDraw` |
| [5. 工程、元数据与发布](tutorial/05-project-and-ship.md) | 一个可发布的 `.dll` 扩展，有图标、版本号和 Ribbon 位置 | `IPlugin`、`PluginMetadata`、`RibbonPlacement` |

---

## 3. 五分钟后想上手

只想先看见东西动起来：新建一个文档 → 打开**视图 → 面板 → 命令控制台**（`Ctrl+Shift+J`）→ 敲

```csharp
host.Log($"文档 {host.DocumentName}，实体 {host.Entities.Count}，选中 {host.Selection.Count}");
```

控制台和插件共用同一套 `IHost`：这里能写的，插件里都能写。`host` 就是这个全局对象，细节见 [API 参考 §2](api.md#2)。

---

## 4. 读文档的三条主线

写插件时你只需要记住三件事：

```
读     host.Entities / host.Selection / host.Features(id)     ← 只读快照
写     host.Dispatch("命令名", new CommandArgs()…)             ← 唯一写路径
交互   host.Ui / host.BeginPointInput / host.BeginEntityInput  ← 宿主代你弹窗和点选
```

改文档**永远**是 `Dispatch` 一条已经存在的命令——插件不持有 `Document*`，也不 new 实体。这样撤销、重做、刷新视图、和按钮行为一致，都是白送的。

---

## 5. 卡住了看哪里

| 症状 | 看 |
|---|---|
| 插件管理里没有我的按钮 | [使用 §3](usage.md#3) |
| 按钮点了没反应 / 报 `no active document` | 先新建或打开文档；[IHost §7](api/host.md) |
| `dispatch` 报 `unknown command` | 命令名拼错了，对照[命令与参数](api/commands.md) |
| 改了 `main.cs` 没生效 | [使用 §1.1 自动重载](usage.md#11) |
| 改完 C# 重启也没变（预编译） | 重新 publish；只 build `tamias` 不会重编 C# 插件 |
| 想知道能用哪些方法/全局变量 | [API 参考](api.md) |

下一步：[1. 第一个扩展](tutorial/01-first-extension.md) ·
另一套：[API 参考](api.md) · [设计理念](design.md) · [使用](usage.md)
