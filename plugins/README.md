# 插件示例

这个目录放**插件示例**，与插件 SDK（`plugin-sdk/csharp/Tamias.Api`、
`plugin-sdk/csharp/Tamias.Host`）分开。子目录按**语言**分（当前只有 `csharp/`），
与"插件装在哪"无关——运行时的两个约定根是 `<exe>/plugins/`（内置）和
`<AppData>/tamias/tamias/extensions/`（用户装的）。

当前内容（都在 `csharp/` 下，两种形态各占一个例子）：

- `csharp/Tamias.Hello/` — **预编译**插件（csproj → DLL）：实现 `IPlugin`，
  覆盖选择、宿主对话框、脚本化建墙、视口拾对象
- `csharp/Tamias.Nurbs/` — 预编译插件：视口拾点示例；把 NURBS 命令注入 `home/draw`，
  收集控制点后 dispatch 通用 `create_curve`
- `csharp/Tamias.Sample.Tools/` — **源码**扩展（没有工程文件：`extension.json` +
  `main.cs` + 图标，加载时用 Roslyn 现编译）：入口是 `public static void Load(IHost)`，
  元数据来自清单，所以不实现 `IPlugin`

两种形态是同一套加载机制，差别只在打包：要发布给别人、要引第三方库、要稳定版本号就出
DLL；想改一行保存就能用就写 `main.cs`。

写新插件时复制一个示例目录，把程序集 publish 到 `exe/plugins/`（或交给 CMake 的
`tamias_publish_csharp`），启动时由托管宿主自动扫描加载。

接口契约见 `plugin-sdk/csharp/Tamias.Api/`（`IPlugin` / `IHost`），文档见 `docs/plugin/`。
