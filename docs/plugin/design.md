# 插件：设计理念

> Tamias 的扩展点在**命令之上**，不在几何内核里。插件问「现在选了谁」、然后 `Dispatch` 一条已经存在的命令。OCCT、特征树、场景图、RHI 继续只被 C++ 拥有。

这是 Revit API / RhinoCommon 那一派：**宿主提供窄契约，脚本改文档，不改引擎。**

---

## 1. 为什么不是「在 C++ 里嵌解释器」

三维软件里，脚本很容易长成第二条内核：直接改网格、绕过撤销、和 UI 抢文档。

Tamias 已经有一条编辑主线（见[命令与撤销](../tutorial/07-commands-and-undo.md)）：

```
UI / 插件
  → CommandSystem.dispatch
    → Command::execute
      → Document / Entity / BIM
        → 造型重算 → 视口刷新
```

插件必须走同一条线，才能免费得到：

| 得到什么 | 原因 |
|---|---|
| 撤销 / 重做 | `dispatch` 成功的非交互命令会进命令栈 |
| 和按钮一致的行为 | `delete_entity` 不论谁发，都是同一个 `DeleteEntityCommand` |
| 可测试 | `HostApi` 不依赖 CLR，C++ 测试能直接调函数指针 |
| 内核封闭 | 插件看不到 `TopoDS_Shape`、RHI、特征树内部 |

所以宿主暴露窄能力：**读文档快照**、**写选择**、**宿主驱动的视口输入**、**宿主 Qt 对话框**，以及 **按名字派发命令**。

---

## 2. 分层：插件停在哪

```
┌────────────── C# 插件 (plugins/*.dll) ─────────┐
│  IPlugin.Load → AddCommand / BeginPointInput / IUi     │
├────────────── Tamias.Host (managed/) ───────────┤
│  hostfxr 入口 · ALC 加载 · 把 HostApi 包成 IHost │
├────────────── C ABI  HostApi  (稳定面) ─────────┤
├────────────── PluginHost (src/plugin) ──────────┤
│  绑当前 Document + CommandSystem · 刷新视口      │
├────────────── 命令 / 文档 / 造型 / 渲染 ─────────┤
│  原样，不给插件开后门                              │
```

和上下的契约：

| 方向 | 做什么 | 不做什么 |
|---|---|---|
| 对上（C#） | `IHost`：文档名、实体列表、选择读写、日志、Ribbon、拾点/拾对象、宿主对话框、dispatch | 不暴露 Qt 句柄、相机、GPU、OCCT |
| 对下（C++） | `HostApi` 函数指针；`dispatch` 进 `CommandSystem` | 不让插件持有 `Document*` |
| 对 UI | 启动时扫插件；命令可进入指定 Ribbon page/group；视口代插件采集点/对象；`IUi` 由宿主弹出 Qt 对话框；日志进状态栏 | 不给插件自建 HWND / 嵌入 WinForms |

稳定面是 **C ABI**（[`host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)），不是 C++ 类布局，也不是 C++/CLI。C# 用 P/Invoke 函数指针；以后用 Rust / 纯 C 插件也可以对同一张表。

当前加载器只扫 `.dll` 里的 `IPlugin`。原生插件若要接，需自己吃 `HostApi`；仓库里的产品路径是 C#。

---

## 3. 三条铁律

1. **插件不持有内核对象。** 看到的是 id + 种类 + 名字。要改文档，发命令。
2. **命令名是公共协议。** `delete_entity`、`set_param` 和工具条用同一套注册表（[`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/register_commands.cpp)）。
3. **宿主失败不能拖死应用。** 找不到 nethost / `managed/Tamias.Host.dll` 时只打日志，主程序照常开。没有插件命令而已。

ABI 版本现在是 `5`。C# `Bootstrap.Initialize` 对不上就拒绝加载。v5 在 v4 的 metadata 上追加：写选择、宿主对话框（消息/输入/表单/文件）、实体拾取与更丰富的点输入预览；创建类命令在参数给齐点列/`origin` 时改为立即 execute。

---

## 4. 一次点击怎么走完

以示例「删除所选」为例：

```
用户点 Ribbon「删除所选」
  → MainWindow 绑当前视口的 Document + CommandSystem
    → PluginHost.invoke("hello.delete_selected")
      → Tamias.Host.Bootstrap.Invoke
        → HelloPlugin 读 IHost.Selection
          → 对每个 id：Dispatch("delete_entity", entity_id=…)
            → HostApi.dispatch → parse 参数文本 → CommandSystem.dispatch
              → DeleteEntityCommand::execute（进撤销栈）
                → after_edit：同步网格 / 重算场景 / BVH / 重绘
```

插件代码里没有「删节点」；它只点了内核已经会的那颗按钮。

插件绘制采用两段式流程：先由 `BeginPointInput` / `BeginEntityInput` 让宿主采集并预览输入（可先用 `IUi.ShowForm` 填尺寸），再由完成回调 `Dispatch` 或 `HostDraw` 发一条给齐参数的文档命令。Qt、拾取和撤销边界仍由宿主控制。给齐 `p:points` / `v:origin` 时，`create_wall` / `create_box` / `create_line` 等不再武装交互工具，而是立刻 execute。

---

## 5. 和「几何内核插件口」不是一回事

造型层的 [`IShapeOps`](../ISHAPE-OPS.md) 是 **C++ 内核换几何后端**（OCCT / 将来 IfcOpenShell）。  
这里的插件是 **用户扩展**：自动化、批处理、领域小工具。

不要把脚本写进 `IShapeOps`，也不要在插件里重写布尔运算。几何继续走命令 → 造型。

下一篇：[使用](usage.md)
