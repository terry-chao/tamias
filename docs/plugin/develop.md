# 插件：开发

> 两条路，先选一条：
>
> - **源码扩展**：一个 `main.cs`（+ 可选 `extension.json`）扔进约定目录，加载时现编译。**不用工程、不用编译**，写工具最快。
> - **预编译扩展**：一个 class library，实现 `IPlugin`，publish 到 `plugins/`。要打包发布、要第三方依赖时走这条。
>
> 两条路进的是同一套东西：同一份 `IHost`、同一张扩展清单、同一个管理界面。

---

## 0. 源码扩展：一个文件就能开工

```
%APPDATA%/tamias/tamias/extensions/my.tools/
├── extension.json     清单（可选：删掉就用目录名当 id 和名称）
└── main.cs            入口
```

```json
{
  "id": "my.tools",
  "name": "我的工具",
  "version": "1.0.0",
  "author": "Me",
  "releaseDate": "2026-09-25",
  "description": "把选中构件的拉伸深度改一改。",
  "icon": "icon.svg"
}
```

```csharp
using Tamias.Api;

// 入口约定：程序集里任意一个类型带 `public static void Load(IHost)`。
public static class Entry
{
    static string source_ = "";

    public static void Load(IHost host)
    {
        source_ = ExtensionContext.SourcePath;  // 自己的目录（只在 Load 期间有效，先存下来）
        host.AddCommand("my.thicken", "加厚", () => Thicken(host), "把所有拉伸深度 +0.1");
    }

    static void Thicken(IHost host)
    {
        using var tx = host.BeginTransaction("加厚");
        foreach (var id in host.Selection.ToList())
        {
            foreach (var f in host.Features(id).Where(f => f.Kind == FeatureKind.Extrude))
            {
                if (!f.Params.Any(p => p.Name == "depth")) continue;
                host.Dispatch("set_param", new CommandArgs()
                    .SetInt("entity_id", (long)id)
                    .SetInt("feature_id", (long)f.Id)
                    .SetString("param_name", "depth")
                    .SetDouble("value", f.Params.First(p => p.Name == "depth").Value + 0.1));
            }
        }
        tx.Commit();
    }
}
```

约定与坑：

- **元数据来自清单**，所以源码扩展**不要**自己调 `host.RegisterPlugin`（清单已经登记过了，重复会失败）。
- 入口是 `public static void Load(IHost)`，返回 `void`；一个程序集里有多个也能都跑到。
- `ExtensionContext.SourcePath` 只在 `Load` 期间有效——命令回调里要用就先存进字段。
- 加载时现编译，所以启动会慢一点点（一个扩展约 0.1–2 秒，取决定义了多少东西）。编译不过只影响它自己，别的扩展照常。
- 改完**不用重启**：保存 `main.cs` 就会自动重载（见[使用 §1.1](usage.md)）。编译不过时会留着旧版本继续用，只在状态栏报一行错。

现成例子：[`plugins/extensions/Tamias.Sample.Tools`](https://github.com/terry-chao/tamias/tree/main/plugins/extensions/Tamias.Sample.Tools)（随构建拷进 `<exe>/plugins/`）。

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
- 需要改尺寸时用 `set_param`（要知道 `feature_id` 和参数名）。**v6 起可以枚举**：`host.Features(entityId)` 给出特征 id、种类、依赖的上游 id 和各参数的当前值，不用再自己猜 id 或对着属性面板抄。

```csharp
// 从枚举到派发走一遍：把选中实体每个特征参数都加宽 0.1。
foreach (var entityId in host.Selection.ToList())
{
    foreach (var feature in host.Features(entityId))
    {
        foreach (var param in feature.Params)
        {
            host.Dispatch("set_param", new CommandArgs()
                .SetInt("entity_id", (long)entityId)
                .SetInt("feature_id", (long)feature.Id)
                .SetString("param_name", param.Name)
                .SetDouble("value", param.Value + 0.1));
        }
    }
}
```

批量改参数要包在事务里，否则每条 `set_param` 都是一步撤销：

```csharp
using var tx = host.BeginTransaction("批量改参数");
// …上面那圈 Dispatch…
tx.Commit();  // 整批只占一步撤销；不 Commit 就是整批回滚
```

完整可运行样本：[`HelloPlugin.cs`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs)。命令与参数表见[宿主功能](api.md)。

---

## 3. Ribbon 与视口拾点

命令缺省进入 `home/plugins`（开始 → 插件）。传入 `RibbonPlacement("home", "draw")` 可加入现有“开始 → 绘制”组；page/group 使用稳定 id，不使用翻译后的标题。

绘制插件通过非阻塞拾点接口编排，鼠标事件、工作面、吸附和取消仍由宿主管理。尺寸可先用 `IUi.ShowForm` 询问：

```csharp
var form = new PromptForm { Title = "创建墙" }
    .AddNumber("thickness", "厚度 (m)", 0.2, 0.01, 5)
    .AddNumber("height", "高度 (m)", 3, 0.1, 50);
if (!host.Ui.ShowForm(form)) {
    return;
}
host.BeginPointInput(new PointInputOptions {
    MinPoints = 2,
    MaxPoints = 2,
    GridSnap = true,
    PreviewKind = PointInputPreviewKind.Wall,
}, result => {
    if (!result.Cancelled) {
        host.Wall(result.Points[0], result.Points[1],
                  form.Number("thickness"), form.Number("height"));
    }
});
```

拾对象：

```csharp
host.BeginEntityInput(new EntityInputOptions {
    MinCount = 1,
    MaxCount = 0,
    AllowConfirm = true,
    FilterKind = EntityKind.Wall,
}, result => {
    if (!result.Cancelled) {
        host.SetSelection(result.EntityIds);
    }
});
```

每个视口只能有一个活动请求。新请求、切换文档、Esc 或右键都会取消旧请求并回调 `Cancelled=true`。

---

## 4. 调试

想先试一句再写进插件：**视图 → 面板 → 命令控制台**，直接敲 C#（每段自带一个事务，改错一步撤销）。控制台和插件共用同一套 `IHost`。

- 插件异常会被 `Bootstrap.Invoke` 吃掉并 `Log` 到状态栏，不会崩进程。
- 可以在 Visual Studio / Rider 里对 `tamias.exe` 附加进程，断点打在插件工程（需 pdb 和 DLL 一起放到 `plugins/`）。
- 改 C# 后重新 publish 再重启；hostfxr 不会热重载 ALC（加载上下文 `isCollectible: false`）。
- 只测参数解析和 `HostApi` 派发、不启动 CLR：`tamias_tests --gtest_filter=CommandArgText*:PluginHost*`。整套测试怎么跑、还缺什么，见 [测试](../TESTING.md)。

C++ 侧入口：[`PluginHost`](https://github.com/terry-chao/tamias/blob/main/src/plugin/plugin_host.h) 的 `load` / `invoke` / `dispatch`；CLR 在 [`csharp_runtime.cpp`](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.cpp)（`Tamias.Host.Bootstrap.Initialize` / `Invoke`）。

---

## 5. 现在不要做的事

| 想做 | 现状 |
|---|---|
| 自建 WinForms/WPF 窗口、Dock | 用 `IUi`（消息/表单/文件框），窗口由宿主 Qt 弹出 |
| 自己订阅 Qt 鼠标事件 | 使用 `BeginPointInput` / `BeginEntityInput` |
| 读网格、变换矩阵 | 没有。特征树与参数 v6 已可读（`IHost.Features`），几何和矩阵仍不在契约里 |
| 改相机 | 没有 |
| 在插件里 new 实体对象 | 必须 `Dispatch` 或 `HostDraw` |
| 依赖另一份 `Tamias.Api.dll` | ALC 强制用宿主那份；不要把 API 拷进 `plugins/` |

这些要加的话，先扩 `HostApi` 并 **把 `kHostApiVersion` 加一**，C# `HostApi` 结构体同步改。不要在 v7 表中间插字段。

设计背景见[设计理念](design.md)。
