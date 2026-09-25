# 插件

C++ 内核不动。C# 做插件 / 脚本宿主。对标 Revit / Rhino：扩展写在托管侧，编辑走已有命令。

扩展从**约定目录**加载，两种形态走同一套机制：`main.cs` 源码（加载时现编译）或 `.dll` 预编译。
内置的放 `<exe>/plugins/`，用户装的放 `<AppData>/tamias/tamias/extensions/`；同 id 时**用户那份覆盖内置**。

- [设计理念](design.md) —— 为什么是宿主而不是内核脚本、稳定面在哪
- [使用](usage.md) —— Ribbon「插件」页、示例命令、运行时目录
- [宿主功能](api.md) —— `IHost` 能查什么、能 `Dispatch` 哪些命令
- [开发插件](develop.md) —— 实现 `IPlugin`、参数格式、部署

代码：[`src/plugin/`](https://github.com/terry-chao/tamias/tree/main/src/plugin)（C++ 宿主）、[`plugin-sdk/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp)（`Tamias.Api` / `Tamias.Host` 插件 SDK）、[`plugins/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugins/csharp)（预编译扩展示例，如 `Tamias.Hello`）、[`plugins/extensions/`](https://github.com/terry-chao/tamias/tree/main/plugins/extensions)（源码扩展示例 `Tamias.Sample.Tools`）。

想先试一句再写插件：**视图 → 面板 → 命令控制台**（`Ctrl+Shift+J`）里直接敲 C#，
和插件共用同一套 `IHost`。见[脚本与命令控制台](../SCRIPTING.md)。
