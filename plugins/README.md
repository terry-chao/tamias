# 插件示例

这个目录放**插件示例**，与插件 SDK（`plugin-sdk/csharp/Tamias.Api`、
`plugin-sdk/csharp/Tamias.Host`）分开。

当前内容：

- `csharp/Tamias.Hello/` — C# 示例插件（实现 `IPlugin`，注册两条 Ribbon 命令）

写新插件时复制一个示例目录，把程序集 publish 到 `exe/plugins/`（或交给 CMake 的
`tamias_publish_csharp`），启动时由托管宿主自动扫描加载。

接口契约见 `plugin-sdk/csharp/Tamias.Api/`（`IPlugin` / `IHost`），文档见 `docs/plugin/`。
