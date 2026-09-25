# 5. 工程、元数据与发布

> 什么时候该从 `main.cs` 升级到 `.dll`：要发布给别人用、要引用第三方库、要稳定的版本号和图标。这一章给出最小工程、Ribbon 落位、发布与调试。

代码基本能从源码扩展原样搬过来——`Load(IHost)` 里写的东西不变，只是外面多了个工程和一个 `IPlugin` 类。

---

## 1. 最小工程

建议放在 `plugins/csharp/` 下，和示例一样用仓库根目录的 [`Directory.Build.props`](https://github.com/terry-chao/tamias/blob/main/Directory.Build.props)（`net8.0`）。

```xml
<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <RootNamespace>MyCompany.TamiasPlugin</RootNamespace>
    <AssemblyName>MyCompany.TamiasPlugin</AssemblyName>
    <CopyLocalLockFileAssemblies>false</CopyLocalLockFileAssemblies>
  </PropertyGroup>
  <ItemGroup>
    <ProjectReference Include="..\Tamias.Api\Tamias.Api.csproj">
      <Private>false</Private>
      <ExcludeAssets>runtime</ExcludeAssets>
    </ProjectReference>
  </ItemGroup>
</Project>
```

两个关键点：

- **`Tamias.Api` 由宿主提供**，不要把 `Tamias.Api.dll` 拷进 `plugins/`（`ExcludeAssets=runtime` 就是干这个的）。加载器会强制把你的程序集解析到宿主那一份，否则 `IHost` 会变成两个互不认的类型。
- 类型必须 **public**、有无参构造，否则扫不到。一个 DLL 可以有多个 `IPlugin`，都会 `Load`。

```csharp
using Tamias.Api;

namespace MyCompany.TamiasPlugin;

public sealed class MyPlugin : IPlugin
{
    public PluginMetadata Metadata => new()
    {
        Id = "mycompany.report",
        Name = "报告工具",
        Author = "My Company",
        Version = "1.0.0",
        ReleaseDate = "2026-09-25",
        Description = "汇总当前文档信息。",
        HomepageUrl = "https://example.com/tamias-plugin",
        IconPath = "plugin.svg",
    };

    public void Load(IHost host)
    {
        host.AddCommand("my.report", "汇报文档",
            () => host.Log($"文档 {host.DocumentName}，实体 {host.Entities.Count}，选中 {host.Selection.Count}"),
            "把文档规模写到状态栏");
    }
}
```

---

## 2. `IPlugin` 与 `PluginMetadata`

```csharp
public interface IPlugin
{
    PluginMetadata Metadata => new();
    void Load(IHost host);
}
```

| `PluginMetadata` 字段 | 说明 |
|---|---|
| `Id` | 稳定标识，插件管理里启停设置的持久化键。**发布后保持稳定** |
| `Name` | 显示名 |
| `Author` | 作者 |
| `Version` | 版本号 |
| `ReleaseDate` | `yyyy-MM-dd`；格式不对会被丢掉并记一条日志 |
| `Description` | 描述 |
| `HomepageUrl` | 只接受绝对 `http/https` URL，否则丢掉 |
| `IconPath` | 图标；**相对路径按插件 DLL 所在目录解析** |
| `IsBuiltIn` | 仅供随 Tamias 一起发布的官方插件标记。实际取值由**所在根目录**决定，扩展自报无效，也不改变权限 |

元数据**代码优先**（就是上面这个 `Metadata`）：缺的字段用同目录的 `extension.json` 补，
再缺就退回程序集版本 / 类型名（源码扩展退回目录名）。两种形态同一条规则。

---

## 3. Ribbon 落位 `RibbonPlacement`

```csharp
host.AddCommand("tamias.nurbs.create", "NURBS", () => BeginCreate(host),
    "从控制点创建 NURBS",
    new RibbonPlacement
    {
        PageId = "home",
        GroupId = "draw",
        IconPath = iconAbsolutePath,
        Order = 700,
        Checkable = true,
    });
```

| 字段 | 默认 | 说明 |
|---|---|---|
| `PageId` | `"home"` | Ribbon 页 id。`"plugins"` 是退役写法，会被改到 `home` |
| `GroupId` | `"plugins"` | 组 id。`"commands"` / `"manage"` 也是退役写法，会被改到 `plugins` |
| `IconPath` | 空 | 图标路径；空则用默认图标。**相对路径按插件 DLL 目录解析**，预编译插件建议像示例那样传绝对路径 |
| `Order` | 0 | 同组内排序键，小的在左 |
| `Checkable` | false | 按钮可选中（用来表示"这个工具正开着"，例如 NURBS） |

内置 page / group 的 id（用稳定 id，不要用翻译后的标题）：

| PageId | 组 |
|---|---|
| `home` | `file`、`draw`、`architectural`、`structural`、`modify`、`annotate`、`navigation`、`settings`、`plugins`、`help` |
| `view` | `display`、`labels`、`panels`、`workspace` |

page 或 group **不存在时会自动新建**，标题就是那串 id——所以想自己开一页是允许的，只是标题会是英文 id。

---

## 4. 发布到哪里

两个根目录，按顺序扫：

| 根 | 谁放的 | 说明 |
|---|---|---|
| `<exe>/plugins/` | 随版本发布 | 这里的东西算**内置**（`built_in = true`） |
| `%APPDATA%/tamias/tamias/extensions/` | 用户自己 | 同 id 会**覆盖**内置那份，并记一条日志 |

每个根里两种形态都认：

- 预编译扩展：`*.dll`（顶层平铺）或 `<名字>/<名字>.dll`（目录形式）
- 源码扩展：`<名字>/main.cs`（+ 可选 `extension.json`）
- 总入口：根顶层的 `loader.cs`——见下

发布命令（在自己的工程目录）：

```
dotnet publish -c Release -o "<Tamias 安装目录>/plugins"
```

仓库里的示例走 CMake 的 `tamias_publish_csharp`，构建 `tamias` 目标时会自动把 `Tamias.Hello` / `Tamias.Nurbs` publish 过去。也可以在自己的 CMake 里加一条同样的 `dotnet publish`。

**开发期不想来回 publish**：工程留在自己的目录，在用户根放一个 `loader.cs` 指过去，
改完源码保存就生效（细节见[使用 §1.2](../usage.md#12-loader一个-cs-文件决定加载谁)）：

```csharp
// %APPDATA%/tamias/tamias/extensions/loader.cs
using Tamias.Api;

public static class Entry
{
    public static void Load(IHost host)
    {
        host.LoadExtension(@"C:\dev\myplugin");          // 工程目录（里面有 main.cs）
        // 编译型工程指向它的输出：那里面才是 .dll
        host.LoadExtension(@"C:\dev\myplugin2\bin\Debug\net8.0\MyPlugin2.dll");
    }
}
```

---

## 5. 调试

| 手段 | 怎么做 |
|---|---|
| 先试一句 | **视图 → 面板 → 命令控制台**（`Ctrl+Shift+J`），直接敲 C#；和插件共用同一套 `IHost` |
| 看日志 | `host.Log(...)` 落到状态栏（约 8 秒）和控制台面板 |
| 断点 | 附加到 `tamias.exe`，把 pdb 和 DLL 一起放进 `plugins/` |
| 改 C# 之后 | 重新 publish 再重启；hostfxr 不会热重载预编译扩展的加载上下文 |
| 只测参数解析 / ABI | `tamias_tests --gtest_filter=CommandArgText*:PluginHost*` |

插件抛异常不会崩进程：`Bootstrap.Invoke` 会接住并把消息 `Log` 到状态栏。所以状态栏那行往往就是你的第一现场。

---

## 6. 现在不要做的事

| 想做 | 现状与替代 |
|---|---|
| 自建 WinForms / WPF 窗口、Dock | 用 `IUi`（消息 / 表单 / 文件框），窗口由宿主 Qt 弹出 |
| 自己订阅 Qt 鼠标事件 | 用 `BeginPointInput` / `BeginEntityInput` |
| 读网格、变换矩阵 | 没有。特征树和参数可读（`IHost.Features`），几何和矩阵不在契约里 |
| 改相机 | 没有 |
| 在插件里 new 实体对象 | 必须 `Dispatch` 或 `HostDraw` |
| 依赖另一份 `Tamias.Api.dll` | 加载上下文强制用宿主那份；不要把 API 拷进 `plugins/` |
| 在 `Load` 里 `Dispatch` | 启动时可能还没有文档；把动作放进命令回调 |

这些要加的话，先扩 `HostApi` 并把 `kHostApiVersion` 加一，见 [C ABI §6](../api/abi.md)。

---

## 7. 完整样例

| 样例 | 看点 |
|---|---|
| [`Tamias.Hello`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs) | 最小 `IPlugin`：选择、特征、事务、对话框、视口拾点、建墙 |
| [`Tamias.Nurbs`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Nurbs/NurbsPlugin.cs) | 往 `home/draw` 插按钮 + `Checkable` + 收集控制点后发 `create_curve` |
| [`Tamias.Sample.Tools`](https://github.com/terry-chao/tamias/tree/main/plugins/csharp/Tamias.Sample.Tools) | 目录式源码扩展：清单 + `main.cs` + 图标 |

---

回到：[教程目录](../tutorial.md) · 另一套：[API 参考](../api.md) · 上线前再过一眼：[设计理念](../design.md)
