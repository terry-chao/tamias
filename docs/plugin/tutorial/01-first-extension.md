# 1. 第一个扩展

> 源码扩展：一个目录 + 一个 `main.cs`，不用工程、不用编译。写完保存就能在 Ribbon 上点。

---

## 1. 建目录

扩展放在两个约定位置之一：

| 位置 | 谁放的 |
|---|---|
| `<exe>/plugins/` | 随版本发布的内置扩展 |
| `%APPDATA%/tamias/tamias/extensions/` | 用户自己装的（首次启动会自动建好） |

我们放用户目录，Windows 上是：

```
%APPDATA%/tamias/tamias/extensions/my.tools/
├── extension.json      清单（可选：删掉就用目录名当 id 和名称）
└── main.cs             入口
```

`my.tools` 这个目录名会变成扩展的默认 id 和名称。

---

## 2. 写清单

`extension.json` 整个文件是可选的，但它决定插件管理里显示什么：

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

| 字段 | 说明 |
|---|---|
| `id` | 稳定标识，插件管理里启停设置就认它；发布后别改 |
| `name` | 显示名 |
| `version` / `releaseDate` | `releaseDate` 用 `yyyy-MM-dd`，格式不对会被丢掉并记一条日志 |
| `author` / `description` | 显示用 |
| `homepage` | 只接受绝对 `http/https` URL |
| `icon` | 相对扩展目录的图标路径（`.svg` / `.png`） |
| `entry` | 入口文件名，默认 `main.cs` |

**元数据来自清单，所以源码扩展不要自己调 `host.RegisterPlugin`**——清单已经登记过了，重复登记会失败。

---

## 3. 写入口

```csharp
using Tamias.Api;

// 入口约定：程序集里任意一个类型带 `public static void Load(IHost)`。
// 不用实现 IPlugin，也不用工程文件。
public static class Entry
{
    public static void Load(IHost host)
    {
        host.AddCommand(
            "my.hello",
            "打个招呼",
            () => host.Log("你好，来自 my.tools"),
            "点一下往状态栏写一行");
    }
}
```

就这些：

- `AddCommand(id, title, action, tooltip?)` 登记一个 Ribbon 按钮，省略位置就进 **开始 → 插件**。
- 命令 id 用 `作者或包名.动词`，避免和别的插件撞名；重名会登记失败。
- 回调是一个 `Action`，点按钮时在 UI 线程执行。

`Load` 的约定：

| 约定 | 为什么 |
|---|---|
| 返回 `void`，参数 `IHost` | 这是源码扩展的入口签名 |
| 一个程序集里有多个 `Load(IHost)` 也能都跑到 | 所以别在多个类里登记同一个命令 id |
| `Load` 里只登记命令，不要 `Dispatch` | 启动时可能还没有文档 |
| 回调里要用的东西（`host`、自己的目录）在 `Load` 里捕获或存字段 | 回调是稍后才执行的 |

---

## 4. 跑起来

1. 打开 Tamias，**新建或打开一个文档**（欢迎页没有活动文档，插件命令会直接失败）。
2. 看 **开始 → 插件** 组，应该多了一个「打个招呼」。
3. 点它，状态栏出现 `你好，来自 my.tools`。

如果没出现，见[使用 §3 没有插件入口时查什么](../usage.md#3)。

---

## 5. 改完不用重启

宿主盯着这两个约定目录。在编辑器里改 `main.cs` 保存，工具当场就是新的：

```
保存 main.cs
  → 侦测到目录有动静（250ms 防抖）
    → 重扫 + 比内容指纹
      ├─ 没变 → 什么都不做
      ├─ 变了 → 先编译
      │    ├─ 编译不过 → 留着旧版本继续用，状态栏报一行错
      │    └─ 编译通过 → 摘掉旧的 → 装上新的 → 重建 Ribbon
      └─ 目录被删 → 扩展连同命令一起摘掉
```

两条要记住的：

- **编译不过不会毁掉正在用的版本**（保存到一半就是语法错误，那会儿最需要旧的还在）。
- **重载会丢静态状态**：每次都是全新的加载上下文，扩展里的 `static` 字段回到初始值。要跨重载保留，就写进文件。

细节和已知限制见[使用 §1.1](../usage.md#11)。

---

## 6. 顺手记一句：从哪加载的

`Load` 里能拿到自己的目录——`ExtensionContext.SourcePath`：

```csharp
static string source_ = "";

public static void Load(IHost host)
{
    source_ = ExtensionContext.SourcePath;   // 只在 Load 期间有效，先存下来
    host.AddCommand("my.where", "我从哪来", () => host.Log($"扩展目录：{source_}"));
}
```

`Load` 一返回作用域就还回去了（那时可能正在装下一个扩展），所以**回调里要用就必须先存进字段**。

---

## 7. 现成例子

[`plugins/extensions/Tamias.Sample.Tools`](https://github.com/terry-chao/tamias/tree/main/plugins/extensions/Tamias.Sample.Tools) 就是一个完整目录式扩展（清单 + `main.cs` + 图标），随构建拷进 `<exe>/plugins/`。

---

下一章：[2. 读文档](02-read-document.md)（用 `host.Entities` / `Selection` / `Features` 做两个真命令）
