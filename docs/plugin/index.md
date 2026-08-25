# 插件

C++ 内核不动。C# 做插件 / 脚本宿主。对标 Revit / Rhino：扩展写在托管侧，编辑走已有命令。

- [设计理念](design.md) —— 为什么是宿主而不是内核脚本、稳定面在哪
- [使用](usage.md) —— Ribbon「插件」页、示例命令、运行时目录
- [宿主功能](api.md) —— `IHost` 能查什么、能 `Dispatch` 哪些命令
- [开发插件](develop.md) —— 实现 `IPlugin`、参数格式、部署

代码：[`src/plugin/`](https://github.com/terry-chao/tamias/tree/main/src/plugin)（C++ 宿主）、[`plugin-sdk/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugin-sdk/csharp)（`Tamias.Api` / `Tamias.Host` 插件 SDK）、[`plugins/csharp/`](https://github.com/terry-chao/tamias/tree/main/plugins/csharp)（插件示例，如 `Tamias.Hello`）。
