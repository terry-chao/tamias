# Tamias

跨 MCAD / BIM 的**几何查看 + 参数化编辑内核**：把「大模型 BIM 查看」和「参数化几何编辑」放进同一个桌面应用。
Qt 做壳，自研 RHI 跑 Vulkan / OpenGL，OCCT 提供 BRep，IfcOpenShell 提供 IFC 语义，
中间用**特征树**与**分层场景图**串成一条链路。

> 站点首页由模板渲染，见 [`overrides/home.html`](https://github.com/terry-chao/tamias/blob/main/overrides/home.html)；
> 样式在 `docs/css/home.css`。这一页保留一份文字版说明，供搜索与 GitHub 浏览用。

## 这个软件能做什么

| 能力 | 说明 | 文档 |
|---|---|---|
| **参数化编辑** | 几何的源头是特征树（参数 + 依赖）：改参数 → 求值重算 → 渲染；BRep / 三角网只是缓存 | [特征树求值器](FEATURE-TREE-EVALUATOR.md) |
| **BIM 业务层** | 楼层、轴网、墙梁板柱、门窗宿主与关联关系；墙-墙转角自动斜接 | [BIM 业务层](BIM.md) |
| **大模型查看** | 语义树与渲染场景图分离，空间索引点选 / 框选，视锥剔除 + LOD + 合批 | [空间索引](SPATIAL-INDEX.md) |
| **自研 RHI** | Vulkan 主 / OpenGL 副（独立线程），启动探测、失败降级、驱动块名单；浏览器走 WASM + WebGPU | [RHI 启动](RHI-STARTUP.md) |
| **界面抽象层** | 界面库不写死在壳里：对话框 / 主题 / 文本度量 / 窗口 / 画布先过一层自研契约 `tac`，Qt 只是两套实现之一，设置里选、重启生效 | [tac / Qt](TAC.md) |
| **参考图纸** | PDF / DXF / DWF / SVG / 位图贴进三维视口当底图，另有只读二维图纸页 | [参考图纸](DRAWING.md) |
| **插件与脚本** | C# 扩展：约定目录热加载 `main.cs` / `.dll`，Ribbon 命令、视口输入、宿主对话框 | [插件](plugin/index.md) |
| **AI 助手 / Agent** | 内置对话面板，或以 MCP 接 Claude Desktop / Cursor / Codex；写只走 `dispatch` + 事务，可审批可审计 | [AI 系列](ai/index.md) |
| **通用编辑** | 移动 / 复制 / 旋转 / 镜像 / 阵列 = 选择集 → 一条可撤销命令 | [通用编辑](EDIT-OPERATIONS.md) |
| **格式分工** | 编辑态 `.tdoc`（语义树 + 特征树），交换态 IFC / STEP / IGES / BREP / OBJ / GLB | [路线图](ROADMAP.md) |

## 先读这三篇定坐标

- [路线图](ROADMAP.md) —— 一句话定位、分层关系、里程碑
- [MCAD 与 BIM](DECISION-MCAD-BIM.md) —— 为什么做一个 app、编辑深度怎么分层
- [AI 系列](ai/index.md) —— 把文档/命令/特征树交给 AI，写仍走 `dispatch` + 事务
- [架构](ARCHITECTURE.md) —— 引擎 / 宿主 / 胶水 / 界面

## 想学 C++ 3D 开发

[**Tamias 入门教程**](tutorial/index.md) 把它当成一个完整的教学样例：先跑起来、再读骨架、再按兴趣深入。

- 第 1 章 认识 Tamias：软件由哪些部件组成
- 第 2 章 构建与运行：从源码编译并启动
- 第 3 章 界面与交互：先当用户：放盒子、改参数
- 第 4 章 代码骨架 ★：`main()` 到一帧的分层与数据流
- 第 5–7 章：几何与造型 / 文档与场景 / 命令与撤销
- 第 8 章 渲染管线 ★：网格 → GPU → 像素
- 第 9–10 章：BIM 业务层 / Web 与路线图

每章都是「学什么 + 正文 + 动手练习 + 延伸阅读」，看不懂模块文档时先回到教程对应章节。

## 按模块查文档

```
┌────────────── Qt 客户端 (app) ──────────────┐
├────────── 界面抽象层 (ui/tac · ui/qt) ───────┤
├────────────── 插件 / 脚本宿主 (plugin) ────────┤
├────────────── BIM 业务层 (bim) ──────────────┤
├────────────── 场景图 (document/scene) ────────┤
├────────────── 造型 (modeling) ───────────────┤
│  特征树 · 求值器 · 几何边界 (IShapeOps)        │
│  OCCT (STEP/IGES/BREP)  ·  IfcOpenShell (IFC)  │
├────────────── 渲染 (engine/render) ───────────┤
└───────────────────────────────────────────────┘
```

侧栏的分组就是上图的层：总览 / 入门教程 / 客户端 / 插件 / AI / BIM / 场景图 / 造型 / 渲染 / 工程实践。
散落的「为什么」（场景规模、BRep、mipmap、RHI、插件、拾取、文字、浮点、LOD）集中在 [答疑（Q&A）](FAQ.md)。
