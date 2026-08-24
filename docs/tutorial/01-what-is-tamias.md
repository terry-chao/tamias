# 第 1 章　认识 Tamias：一个三维软件长什么样

> 本章目标：知道 Tamias 是做什么的、它由哪些部件组成、你在学 C++ 3D 开发时会碰到哪些技术。

## 1.1 一句话定位

> 跨 **MCAD / BIM** 的**几何查看 + 参数化编辑内核**。

拆开看：

- **MCAD**：机械设计（Mechanical CAD）。零件、装配、拉伸、倒角、布尔运算。
- **BIM**：建筑信息模型（Building Information Modeling）。墙、梁、板、柱、门、窗、楼层、轴网。
- **几何查看**：把 3D 模型显示在屏幕上，能转、能选、能看。
- **参数化编辑内核**：模型不是一坨死三角，而是**参数配方**——改一个数字，整个模型重算更新。

打个比方：Tamias 是「**FreeCAD（参数化建模）+ Navisworks（BIM 大模型查看）**」的结合体。它想同时服务机械和建筑两个领域。

## 1.2 一个三维软件由什么组成

不管多复杂的 3D 软件（Blender、CAD、游戏引擎），基本都能拆成这几块。Tamias 每一块都有对应代码：

| 部件 | 干什么 | 界面上的体现 | Tamias 里的位置 |
|---|---|---|---|
| **窗口壳** | 开窗口、菜单、面板、工具条 | 整个程序外观 | [`src/app`](https://github.com/terry-chao/tamias/tree/main/src/app)（Qt） |
| **文档** | 一个打开的文件：保存所有数据 | 多标签、保存/打开 | [`src/engine/document`](https://github.com/terry-chao/tamias/tree/main/src/engine/document) |
| **场景图** | 谁是谁的孩子、谁在世界哪里 | 大纲树（将来）、层级的逻辑 | `Scene` / `SceneNode` |
| **造型内核** | 把「参数」变成「形状」 | 放盒子、拖墙、改尺寸 | [`src/engine/modeling`](https://github.com/terry-chao/tamias/tree/main/src/engine/modeling)（OCCT） |
| **渲染器** | 把形状变成屏幕像素 | 你看到的三维视口 | [`src/engine/render`](https://github.com/terry-chao/tamias/tree/main/src/engine/render)（自研 RHI） |
| **命令系统** | 编辑操作可撤销/重做 | 撤销、重做按钮 | [`src/command`](https://github.com/terry-chao/tamias/tree/main/src/command) |
| **插件宿主** | C# 扩展：读选择、dispatch 命令 | Ribbon「插件」页 | [`src/plugin`](https://github.com/terry-chao/tamias/tree/main/src/plugin)、[`csharp/`](https://github.com/terry-chao/tamias/tree/main/csharp) |
| **领域层** | 建筑语义规则 | 窗贴在墙上、墙改了窗跟着改 | [`src/bim`](https://github.com/terry-chao/tamias/tree/main/src/bim) |

**新手最容易犯的错**：以为「三维软件 = 渲染」。其实渲染只是最后一公里；前面还有数据、几何、命令、架构一大堆。

## 1.3 技术栈一览

| 技术 | 在 Tamias 里的角色 | 你会学到的知识点 |
|---|---|---|
| **C++23** | 全项目语言 | 现代 C++ 工程实践 |
| **Qt 6** | 桌面窗口壳 | 桌面 UI、事件循环、原生窗口 |
| **CMake + Presets** | 构建系统 | 多后端构建、依赖管理 |
| **Vulkan / OpenGL / WebGL2** | 三种渲染后端 | 图形 API、RHI 抽象 |
| **OCCT（Open CASCADE）** | 几何内核 | BRep、曲面、布尔、离散化 |
| **IfcOpenShell** | IFC（建筑交换格式）解析 | 语义数据、BIM |
| **.NET 8 + hostfxr** | C# 插件宿主 | 进程内 CLR、ALC 加载、C ABI |
| **Emscripten** | 编译到 WebAssembly | 跨平台、浏览器 3D |

注意：Tamias 没有用现成游戏引擎（Unreal/Unity），也没有用 OSG/VTK 这类渲染库——渲染是**自研 RHI**。这正是学 3D 开发的好素材：你能看到图形 API 之上的完整抽象。

## 1.4 你现在应该建立的全局图

```
┌────────────── Qt 客户端 (app) ──────────────┐  窗口、视口、面板、工具、插件页
├────────────── 插件宿主 (plugin) ────────────┤  C# 扩展：读选择、dispatch 命令
├────────────── 命令 (command) ───────────────┤  可撤销的编辑操作
├────────────── BIM 业务层 (bim) ─────────────┤  建筑语义：楼层/宿主/轴网
├────────────── 场景图 (document/scene) ──────┤  谁是谁的孩子、变换、包围盒
├────────────── 造型 (modeling) ──────────────┤  特征树 + OCCT → BRep → 三角网
├────────────── 渲染 (engine/render) ─────────┤  RHI：Vulkan / OpenGL / WebGL
└─────────────────────────────────────────────┘
```

这张图会贯穿整个教程。后面的每一章，都是在给某一层「放大」。

## 1.5 动手练习

1. 打开 Tamias 界面（或先看[第 3 章](03-ui-and-interaction.md)的截图说明），找出上表里至少 5 个部件对应的界面元素。
2. 用你自己的话向别人解释：为什么「渲染」不是 3D 软件的全部？
3. 打开 [`src`](https://github.com/terry-chao/tamias/tree/main/src) 目录，对照 1.2 的表，指出每个模块在哪。

## 延伸阅读

- [路线图](../ROADMAP.md) 第 0 节：一句话定位与对标物
- [总览](../overview/index.md)：分层架构总入口
- [插件系列](../plugin/index.md)：C# 扩展为什么停在命令之上
- [MCAD 与 BIM 决策](../DECISION-MCAD-BIM.md)：为什么一个软件同时做两个领域

下一章：[构建与运行](02-build-and-run.md)
