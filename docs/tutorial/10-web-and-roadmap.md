# 第 10 章　Web 与路线图：未来在哪里，接着学什么

> 本章目标：知道 Tamias 的另外两条产品线（Web 查看器、wgpu 后端）和里程碑规划，拿到属于你自己的进阶学习路径。

## 10.1 第二条产品线：引擎 WASM + Web 查看器

桌面是 Qt 壳；浏览器是另一条线：**引擎交叉编译到 WebAssembly，UI 画在 HTML canvas 上**，不依赖 Qt。

```
Web UI (web/index.html, React)
  → ViewerHost（相机、文件、提交帧）
    → Document / io（与桌面同一套 .tdoc / OBJ）
      → RenderThread（synchronous，主线程 pump）
        → WebGL2 RHI（GLES 3）
```

`src/engine` 不 include Qt，所以能被复用；丢掉的只是 `src/app`。阶段 1 已落地：浏览器能打开 `.tdoc` / `.obj` 并旋转查看。详见 [Web 查看器](../WEB.md)。

## 10.2 第三渲染后端：wgpu（方案已定）

wgpu（WebGPU 的一种桌面实现）要成为 RHI 的**第三个翻译官**，而不是替换 RHI 抽象：

```
draw_channel → RHIDevice 动词 → wgpu 后端（encoder / bind group / queue / present）
```

`draw_channel`、场景、OCCT 一行不改。方案要点：常量走 uniform buffer、shader 吃 SPIR-V、线框用 native `POLYGON_MODE_LINE`。详见 [wgpu 接入](../WGPU.md)。

## 10.3 里程碑：主线 P1–P4

| 里程碑 | 内容 | 状态 |
|---|---|---|
| M0–M6 | 基础查看、场景图层级化 | ✅ 已完成 |
| **P1** | 特征树 + 求值器（Box + Extrude） | 下一步 |
| **P2** | Fillet / Boolean / Chamfer + 命令化 undo | 未开始 |
| **P3** | 拓扑命名（先索引法，再几何法） | 未开始 |
| **P4** | 特征树序列化进 `.tdoc` + 建模 UI | 未开始 |

读完本教程，你已经把 P1 需要的每一块（命令、特征树、重算、上传、渲染）都见过了——P1 就是「把这些连成一个闭环」。

## 10.4 给新手的进阶路径

按兴趣选一条：

**路线 A：图形 / 渲染方向**

1. 精读 [渲染管线](../RENDERING.md) + [NDC](../NDC.md) + [OpenGL 后端](../OPENGL.md)。
2. 读一本图形学入门（推荐《Fundamentals of Computer Graphics》或 LearnOpenGL）。
3. 动手：给 RHI 加一个新管线（例如透明管线），跑通双后端。

**路线 B：几何内核方向**

1. 精读 [特征树求值器](../FEATURE-TREE-EVALUATOR.md) + [几何边界](../ISHAPE-OPS.md)。
2. 了解 BRep 与拓扑命名（[MCAD 管线](../MCAD-PIPELINE.md)）。
3. 动手：给 `FeatureKind` 加一个新特征类型，从配方到求值器到属性面板全链打通。

**路线 C：BIM / 数据方向**

1. 精读 [BIM 业务层](../BIM.md) + [关联关系](../bim/relations.md)。
2. 读 IFC 规范和 IfcOpenShell 文档。
3. 动手：实现「当前标高」最小版本（bim 层持有 `active_storey_id`，`place_wall` 设置 parent）。

**路线 D：Web 方向**

1. 精读 [Web 查看器](../WEB.md) + 阶段划分。
2. 读 Emscripten 的 embind 文档。
3. 动手：把 `CommandSystem` embind 出来，实现浏览器里的选中/撤销（阶段 2）。

## 10.5 动手练习

1. 把 [`ROADMAP.md`](../ROADMAP.md) 的「下一步 = P1」读一遍，说出 P1 涉及的 4 个模块。
2. 选一条路线 A–D，写下你要精读的 3 篇文章清单。
3. 在浏览器里跑一次 `wasm` preset，打开一个 `.obj`，比较和桌面版的差异。

## 延伸阅读

- [Web 查看器](../WEB.md)：WASM 阶段划分与构建
- [wgpu 接入](../WGPU.md)：第三后端方案
- [路线图](../ROADMAP.md)：完整规划与已决定事项

教程结束。需要查词时，回到[术语速查](glossary.md)。
