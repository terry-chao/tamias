# 插件：使用

> 装好 .NET 8（或更新）运行时之后，打开 Tamias，**开始 → 插件** 里有插件管理，示例命令也在同一组。找不到 nethost 时主程序照常开，只是没有插件命令。

---

## 1. 运行时目录

构建 `tamias` 时 CMake 会 `dotnet publish`，并拷 `nethost.dll`。Debug 下大致是：

```
build/bin/Debug/
  tamias.exe
  nethost.dll
  managed/
    Tamias.Host.dll
    Tamias.Api.dll
  plugins/                    ← 内置根：随版本发布的扩展
    Tamias.Hello.dll          预编译扩展
    Tamias.Nurbs.dll
    nurbs.svg
    Tamias.Sample.Tools/      目录式扩展：清单 + 源码
      extension.json
      main.cs
      icon.svg
```

**两个约定位置**，按顺序扫：

| 根 | 谁放的 | 说明 |
|---|---|---|
| `<exe>/plugins/` | 随版本发布 | 第一个根。这里的东西算**内置**（`built_in = true`） |
| `<AppData>/tamias/tamias/extensions/` | 用户自己 | 首次启动自动建好；同 id 会**覆盖**内置那份，并记一条日志 |

每个根里两种形态都认：

- **预编译扩展**：`*.dll`（顶层平铺）或 `<名字>/<名字>.dll`（目录形式）
- **源码扩展**：`<名字>/main.cs`（+ 可选的 `<名字>/extension.json`），加载时用 Roslyn 现编译。
  元数据写在代码里（入口类型上的静态 `PluginMetadata Metadata`），缺的字段用清单补
- **总入口**：根顶层的 `loader.cs`（可选）。它被执行，里面用 `host.LoadExtension(path)`
  决定还要装哪些工程——那些工程可以放在**任何地方**。见 [§1.2](#12-loader一个-cs-文件决定加载谁)
- **根自己就是一个扩展**：根顶层直接放着入口文件（清单里写的那个，缺省 `main.cs`）时，
  认它一个——`LoadExtension` 指过来的工程目录就是这种摆法；约定根一般不这么用

上面两个根，再加上 loader 用 `LoadExtension` 登记的路径（排在最后，所以不覆盖内置的），
就是本次会话要扫的全部。

扫描与加载规则（[`ExtensionScanner.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/ExtensionScanner.cs) / [`ExtensionLoader.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Host/ExtensionLoader.cs)）：

- **先全扫一遍再加载**，这样 id 冲突能提前发现；后扫的根覆盖先扫的，覆盖时记一条日志（「我改了怎么没生效」能查）
- 跳过 `Tamias.Api*` / `Tamias.Host*` / `System.*` / `Microsoft.*`
- 预编译扩展：程序集里公开、非抽象、实现 `IPlugin` 的类型都会 `Load(IHost)`
- 源码扩展：程序集里任意一个 `public static void Load(IHost)` 就会被调用
- 目录里既没有 `main.cs`（或清单指定的入口）也没有 `.dll` → 跳过并记一条日志
- **一个扩展坏了不拖死别的**：编译不过、入口抛异常都只记日志，其余照常加载

### 1.1 自动重载：改完不用重启

宿主盯着这两个约定目录。在编辑器里改 `main.cs` 保存，工具当场就是新的：

```
保存 main.cs
  → QFileSystemWatcher："这个目录有动静"（250ms 防抖，编辑器保存是一串写操作）
    → 托管侧重扫 + 比内容指纹
      ├─ 指纹没变（临时文件、隔壁文件动了）→ 什么都不做，也不刷日志
      ├─ 指纹变了 → **先编译**
      │    ├─ 编译不过 → 留着旧版本继续用，只在状态栏 / 控制台报一行
      │    └─ 编译通过 → 摘掉旧的（命令 + 加载上下文）→ 装上新的 → 重建 Ribbon
      └─ 目录被删掉 → 扩展连同它的命令一起摘掉
```

几条设计上的取舍：

- **监视与判断分开**：C++ 侧（Qt 事件循环里）只负责说"有动静"，"谁真的变了"由托管侧对入口文件（+ 清单）算 SHA-256 说了算。于是编辑器写临时文件、隔壁扩展动一下都不会触发重载风暴。
- **先编译再摘旧的**：保存到一半就是语法错误，那一下最需要旧工具还在。编译失败不改变任何已装好的扩展。
- **命令要摘干净**：重载时先把旧扩展登记的命令从原生命令表和托管委托表里拿掉——不然①命令 id 会撞，②那些委托钉着旧程序集，加载上下文卸不掉。
- **真的会卸载**：加载上下文是 `isCollectible: true`，摘干净后 `Unload()` + 两次 GC。这是能反复重载的前提。
- **重载完重建 Ribbon**：插件管理里的停用状态与排序照旧生效。

已知限制：

- 预编译扩展的指纹只看那个 `.dll`；它带的依赖 DLL 改了不会被发现（重启才生效）。
- **重载会丢静态状态**：每次都是全新的加载上下文，扩展里的 `static` 字段会回到初始值。要跨重载保留，就写进文件或 `QSettings`（走宿主 API）。

放自己的扩展：拷进上面任一目录即可。`Tamias.Api` 由宿主提供，预编译扩展不要把 API DLL 拷进去（示例 csproj 已 `ExcludeAssets=runtime`）。

### 1.2 loader：一个 cs 文件决定加载谁

工程不想放在约定目录里（它有自己的仓库、自己的布局），就放一个 `loader.cs` 当总入口：

```
%APPDATA%/tamias/tamias/extensions/
  loader.cs                   ← 你改这个
  my.other.tool/main.cs       ← 老的直接摆法照旧
```

```csharp
// loader.cs —— 和源码扩展同一套入口约定，只是位置固定在根顶层
using Tamias.Api;

public static class Entry
{
    public static void Load(IHost host)
    {
        host.LoadExtension(@"C:\dev\myplugin\src\main.cs");          // 指入口文件
        host.LoadExtension(@"C:\dev\myplugin");                      // 也可以指目录：里面得有 main.cs
        host.LoadExtension(@"C:\dev\other\bin\Debug\net8.0\Other.dll");
    }
}
```

`path` 可以是目录、`.cs` 入口文件或 `.dll`。**指目录**时：目录里有入口文件（缺省 `main.cs`）
才算"它自己是一个扩展"，否则当成"装着一批扩展的根"来扫——所以编译型工程的源码目录
（有 `.cs` 但没有 `main.cs`）要么指它的入口文件，要么指它的**输出**（`bin/Debug/net8.0/`
或那个 `.dll`）。**指文件**时那个文件就是入口，它所在目录当扩展目录，同目录的
`extension.json` 照样生效。完整规则见
[IHost §7](api/host.md#7-装别的扩展-loadextension)。

几条要记住的：

- **改 `loader.cs` 就是改"装哪些工程"**：它和别的源码扩展一样被监视，保存后当场重编译重跑。
- **登记的路径跟着重扫走**：改工程里的 `main.cs` 保存即生效，不必碰 `loader.cs`；目录删了，
  扩展连同命令一起摘掉。
- **会话内只记不退**：登记过的路径一直在扫描表里。把某一行从 `loader.cs` 删掉、但目录还在的话，
  那个扩展仍会随重扫加载，要它消失得重启（或者删目录）。
- loader 自己也是一个扩展，可以停用。它的 id 按根区分（`<根目录名>.loader`，比如官方的
  `plugins.loader`、用户根的 `extensions.loader`），所以**官方和用户各放一个不会互相覆盖**；
  两个都会执行，各管各的。
- 路径写错只记一条日志，不影响别的扩展。
- **指到编译型工程的源码目录**（有 `.cs`，但没有 `main.cs` / 清单 / `dll`）会什么都不装，
  并记一条提示：那种工程的入口是它的 **publish 输出**，指 `bin/Debug/net8.0/` 或那个 `.dll`。
- 这是**开发期接工程**的路子；要发布给别人，还是照[教程 5](tutorial/05-project-and-ship.md)
  把 `.dll` 放进某个根。

---

## 2. 界面

1. 新建或打开文档（欢迎页没有活动文档，插件命令会失败）。
2. 插件命令可声明 Ribbon 位置；`Tamias.Nurbs` 的按钮位于 **开始 → 绘制**，Hello 位于 **开始 → 插件**。
3. **开始 → 插件 → 插件管理**：
   - “已安装”页显示 icon、作者、内置标识、版本、发布日期、描述、首页和命令所在栏位；取消勾选会立即停用该插件的所有 Ribbon 命令。
   - “Ribbon 布局”页选择 page/group 后，用上移/下移调整具体插件图标的左右顺序。
   - 点 **确定** 后启停与顺序立即生效并持久化；取消不修改。停用不会卸载程序集，只关闭命令入口。
4. `Tamias.Hello` 提供：

| 按钮 | 命令 id | 做什么 |
|---|---|---|
| **列出选择** | `hello.list_selection` | 把当前选择写到状态栏：`#id 种类 名字` |
| **列出特征** | `hello.list_features` | 把选中实体的特征树与参数写到状态栏（v6 宽读） |
| **加宽参数** | `hello.widen_params` | 把选中实体的所有特征参数 +0.1；整批只占一步撤销（v7 事务） |
| **删除所选** | `hello.delete_selected` | 对每个选中 id `dispatch delete_entity`（可撤销） |
| **关于示例** | `hello.about` | 弹出当前文档摘要对话框 |
| **创建墙** | `hello.create_wall` | 表单填厚度/高度，视口点两点后建墙 |
| **拾取对象** | `hello.pick_entities` | 视口点选对象，Enter 后写入选择 |

没有选择时状态栏提示「未选择对象」。插件 `Log` 也走状态栏（约 8 秒）。对话框由宿主弹出，不要在插件里自建窗口。

NURBS 按钮启动宿主拾点：左键添加控制点，Enter 或双击完成，Esc 或右键取消。完成后生成的实体与内置实体一样支持权重编辑、撤销/重做和文档保存。

切换文档标签时，主窗口会把 `PluginHost` 绑到当前视口；点按钮前会再绑一次。

---

## 3. 没有插件入口时查什么

启动时 `PluginHost::load()` 失败会 `log_warn`，应用继续跑。常见原因：

| 现象 | 原因 |
|---|---|
| 插件管理是空的、没有示例按钮 | `managed/Tamias.Host.dll` 或 runtimeconfig 缺失；nethost 未找到；或 `plugins/` 空 / Hello 没 publish 出来 |
| 点按钮说没有活动文档 | 还在欢迎页，先新建/打开 |
| 点按钮失败、状态栏有英文/中文错误 | `dispatch` 失败（例如实体已删）；或命令 id 未登记 |

本机未装 .NET 运行时时，安装 [.NET 8 桌面运行时](https://dotnet.microsoft.com/download/dotnet/8.0)（或更新，宿主 `RollForward` 为 `LatestMajor`）。

从源码编：Windows 先 `vcvars64`，再 `cmake --build --preset debug --target tamias`。`tamias_csharp` 会随 `tamias` 一起 publish。

---

## 4. 示例插件在做什么

[`HelloPlugin.cs`](https://github.com/terry-chao/tamias/blob/main/plugins/csharp/Tamias.Hello/HelloPlugin.cs) 是最小范本：`Load` 里 `AddCommand`，回调里读 `Selection` / `Entities`、弹 `IUi` 对话框、视口拾点后 `HostDraw.Wall`，或 `Dispatch`。不直接改 `Document`，也不自建窗口。

下一篇：[教程](tutorial.md)（从第一个扩展写到发布）· [API 参考](api.md)（`IHost` 能查什么、能发哪些命令）。
