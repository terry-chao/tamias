# 插件：开发

> 一个 class library，实现 `IPlugin`，引用 `Tamias.Api`（不要拷进输出），publish 到 `plugins/`。启动时宿主用独立 `AssemblyLoadContext` 加载，并把 `Tamias.Api` 统一解析到宿主那一份，避免两份接口类型对不上。

---

## 1. 工程最小集

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
        ReleaseDate = "2026-08-25",
        Description = "汇总当前文档信息。",
        HomepageUrl = "https://example.com/tamias-plugin",
        IconPath = "plugin.svg",
    };

    public void Load(IHost host)
    {
        host.AddCommand(
            "my.report",
            "汇报文档",
            () =>
            {
                host.Log($"文档 {host.DocumentName}，实体 {host.Entities.Count}，选中 {host.Selection.Count}");
            },
            "把文档规模写到状态栏");
    }
}
```

约定：

- 命令 `id` 用 `作者或包名.动词`，避免和别的插件撞名。
- `Load` 里只登记命令，不要在 `Load` 时 `Dispatch`（启动时可能还没有文档）。
- 类型必须 **public**、有无参构造，否则扫不到。
- 一个 DLL 可以有多个 `IPlugin`。
- metadata 的 `Id` 是启停设置的持久化键，应发布后保持稳定。`IconPath` 可相对插件 DLL；`ReleaseDate` 使用 `yyyy-MM-dd`。
- `IsBuiltIn` 仅供随 Tamias 一起发布的官方插件标记，不改变安全权限。

把输出拷到 `<exe>/plugins/` 后重启 Tamias。开发时也可改 CMake `tamias_publish_csharp`，加一条你的 `dotnet publish -o …/plugins`。

---

## 2. 读选择、改文档

```csharp
host.AddCommand("my.delete_walls", "删除选中的墙", () =>
{
    var byId = host.Entities.ToDictionary(e => e.Id);
    var n = 0;
    foreach (var id in host.Selection.ToList())
    {
        if (!byId.TryGetValue(id, out var info) || info.Kind != EntityKind.Wall)
        {
            continue;
        }
        host.Dispatch("delete_entity", new CommandArgs().SetInt("entity_id", (long)id));
        n++;
    }
    host.Log(n == 0 ? "没有选中的墙" : $"已删除 {n} 面墙");
});
```

要点：

- 先 `.ToList()` 再删。边 dispatch 边读 `Selection` 会看到过期列表。
- 每个 `delete_entity` 是一条可撤销命令（一次按钮可能进栈多条）。
- 需要改尺寸时用 `set_param`（要知道 `feature_id` 和参数名）。宿主**还不能**枚举特征树；这一版只能你自己约定 id，或先从属性面板/句柄检查对着看。

完整可运行样本：[`HelloPlugin.cs`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs)。命令与参数表见[宿主功能](api.md)。

---

## 3. Ribbon 与视口拾点

命令缺省进入 `plugins/commands`。传入 `RibbonPlacement("home", "draw")` 可加入现有“开始 → 绘制”组；page/group 使用稳定 id，不使用翻译后的标题。

绘制插件通过非阻塞拾点接口编排，鼠标事件、工作面、吸附和取消仍由宿主管理：

```csharp
host.BeginPointInput(new PointInputOptions {
    MinPoints = 2,
    MaxPoints = 0,
    AllowConfirm = true,
    GridSnap = true,
    PreviewKind = PointInputPreviewKind.Curve,
    PreviewCurveKind = "nurbs",
}, result => {
    if (!result.Cancelled) {
        host.Dispatch("create_curve", new CommandArgs()
            .SetString("curve_kind", "nurbs")
            .SetPoints("points", result.Points));
    }
});
```

每个视口只能有一个活动请求。新请求、切换文档、Esc 或右键都会取消旧请求并回调 `Cancelled=true`。

---

## 4. 调试

- 插件异常会被 `Bootstrap.Invoke` 吃掉并 `Log` 到状态栏，不会崩进程。
- 可以在 Visual Studio / Rider 里对 `tamias.exe` 附加进程，断点打在插件工程（需 pdb 和 DLL 一起放到 `plugins/`）。
- 改 C# 后重新 publish 再重启；hostfxr 不会热重载 ALC（加载上下文 `isCollectible: false`）。
- 只测参数解析和 `HostApi` 派发、不启动 CLR：`tamias_tests --gtest_filter=CommandArgText*:PluginHost*`。

C++ 侧入口：[`PluginHost`](https://github.com/terry-chao/tamias/blob/main/src/plugin/plugin_host.h) 的 `load` / `invoke` / `dispatch`；CLR 在 [`csharp_runtime.cpp`](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.cpp)（`Tamias.Host.Bootstrap.Initialize` / `Invoke`）。

---

## 5. 现在不要做的事

| 想做 | 现状 |
|---|---|
| 自定义属性页 / Dock | 没有 UI API |
| 自己订阅 Qt 鼠标事件 | 使用 `BeginPointInput`，插件不接触 Qt |
| 读特征树、网格、变换矩阵 | `EntityInfo` 只有 id / 种类 / 名字 |
| 开文件、改相机 | 没有 |
| 在插件里 new 实体对象 | 必须 `Dispatch` |
| 依赖另一份 `Tamias.Api.dll` | ALC 强制用宿主那份；不要把 API 拷进 `plugins/` |

这些要加的话，先扩 `HostApi` 并 **把 `kHostApiVersion` 加一**，C# `HostApi` 结构体同步改。不要在 v4 表中间插字段。

设计背景见[设计理念](design.md)。
