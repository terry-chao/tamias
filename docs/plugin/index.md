# 插件

C++ 内核不动。C# 做插件 / 脚本宿主。对标 Revit / Rhino：扩展写在托管侧，编辑走已有命令。

扩展从**约定目录**加载，两种形态走同一套机制：`main.cs` 源码（加载时现编译）或 `.dll` 预编译。
内置的放 `<exe>/plugins/`，用户装的放 `<AppData>/tamias/tamias/extensions/`；同 id 时**用户那份覆盖内置**。
工程想留在自己的目录里，就在用户根放一个 `loader.cs`，里面 `host.LoadExtension(path)` 指过去——
约定目录之外的工程也照样现编译、照样热重载，见[使用 §1.2](usage.md#12-loader一个-cs-文件决定加载谁)。

## 两套文档

| 想干什么 | 去哪 |
|---|---|
| **跟着做一遍**：从"一个能点的按钮"到会读文档、会改文档、会在视口取输入 | [教程 Tutorial](tutorial.md) |
| **查某个类型 / 成员 / 命令**：`IHost`、`EntityInfo`、`PromptForm`、`CommandArgs`、可 dispatch 的命令…… | [API 参考 API Reference](api.md) |

教程五章： [1. 第一个扩展](tutorial/01-first-extension.md) · [2. 读文档](tutorial/02-read-document.md) · [3. 改文档](tutorial/03-edit-and-undo.md) · [4. 对话框与视口输入](tutorial/04-dialogs-and-input.md) · [5. 工程、元数据与发布](tutorial/05-project-and-ship.md)

API 参考： [IHost](api/host.md) · [文档快照与枚举](api/document.md) · [宿主对话框](api/ui.md) · [视口输入](api/input.md) · [命令与参数](api/commands.md) · [C ABI](api/abi.md)

## 其他

- [使用](usage.md) —— Ribbon「插件」页、示例命令、运行时目录、插件管理
- [设计理念](design.md) —— 为什么是宿主而不是内核脚本、稳定面在哪

代码：[`src/plugin/`](https://github.com/terry-chao/tamias/tree/main/src/plugin)（C++ 宿主）、[`plugin-sdk/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp)（`Tamias.Api` / `Tamias.Host` 插件 SDK）、[`plugins/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugins/csharp)（扩展示例：预编译的 `Tamias.Hello` / `Tamias.Nurbs`，源码式的 `Tamias.Sample.Tools`）。

想先试一句再写插件：**视图 → 面板 → 命令控制台**（`Ctrl+Shift+J`）里直接敲 C#，
和插件共用同一套 `IHost`。见[脚本与命令控制台](../SCRIPTING.md)。
