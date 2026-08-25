# 插件：使用

> 装好 .NET 8（或更新）运行时之后，打开 Tamias，Ribbon 会出现 **插件** 页。没有这一页，多半是宿主 DLL 没拷到 exe 旁边。

---

## 1. 运行时目录

构建 `tamias` 时 CMake 会 `dotnet publish`，并拷 `nethost.dll`。Debug 下大致是：

```
build/bin/Debug/
  tamias.exe
  nethost.dll
  managed/
    Tamias.Host.dll
    Tamias.Host.runtimeconfig.json
    Tamias.Api.dll
  plugins/
    Tamias.Hello.dll
```

加载规则（[`PluginLoader.cs`](https://github.com/terry-chao/tamias/blob/main/csharp/Tamias.Host/PluginLoader.cs)）：

- 从 `managed/` 的上一级找 `plugins/`
- 加载该目录下所有 `.dll`
- 跳过 `Tamias.Api*`、`Tamias.Host*`、`System.*`、`Microsoft.*`
- 每个程序集里公开、非抽象、实现 `IPlugin` 的类型都会 `Load(IHost)`

自己编译的插件：把 DLL 放进 **与 `tamias.exe` 同级的 `plugins/`**，重启软件。`Tamias.Api` 由宿主提供，插件工程不要把 API DLL 拷进 `plugins/`（示例 csproj 已 `ExcludeAssets=runtime`）。

---

## 2. 界面

1. 新建或打开文档（欢迎页没有活动文档，插件命令会失败）。
2. 切到 Ribbon **插件 → 命令**。
3. 内置示例 `Tamias.Hello` 提供两条：

| 按钮 | 命令 id | 做什么 |
|---|---|---|
| **列出选择** | `hello.list_selection` | 把当前选择写到状态栏：`#id 种类 名字` |
| **删除所选** | `hello.delete_selected` | 对每个选中 id `dispatch delete_entity`（可撤销） |

没有选择时状态栏提示「未选择对象」。插件 `Log` 也走状态栏（约 8 秒）。

切换文档标签时，主窗口会把 `PluginHost` 绑到当前视口；点按钮前会再绑一次。

---

## 3. 没有插件页时查什么

启动时 `PluginHost::load()` 失败会 `log_warn`，应用继续跑。常见原因：

| 现象 | 原因 |
|---|---|
| 根本没有「插件」页 | `managed/Tamias.Host.dll` 或 runtimeconfig 缺失；或 nethost 未找到，C++ 没定义 `TAMIAS_HAS_NETHOST` |
| 有页但没有示例按钮 | `plugins/` 空，或 Hello 没 publish 出来 |
| 点按钮说没有活动文档 | 还在欢迎页，先新建/打开 |
| 点按钮失败、状态栏有英文/中文错误 | `dispatch` 失败（例如实体已删）；或命令 id 未登记 |

本机未装 .NET 运行时时，安装 [.NET 8 桌面运行时](https://dotnet.microsoft.com/download/dotnet/8.0)（或更新，宿主 `RollForward` 为 `LatestMajor`）。

从源码编：Windows 先 `vcvars64`，再 `cmake --build --preset debug --target tamias`。`tamias_csharp` 会随 `tamias` 一起 publish。

---

## 4. 示例插件在做什么

[`HelloPlugin.cs`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs) 是最小范本：`Load` 里 `AddCommand` 两次，回调里只读 `Selection` / `Entities`，或 `Dispatch`。没有自定义窗口、没有直接改 `Document`。

下一篇：[宿主功能](api.md)（能调哪些 API、能发哪些命令）。要写自己的 DLL，见[开发插件](develop.md)。
