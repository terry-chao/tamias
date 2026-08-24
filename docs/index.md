# Tamias

跨 MCAD / BIM 的**几何查看 + 参数化编辑内核**。

- **Qt** 做桌面壳，自研 RHI（Vulkan 主 / OpenGL 副 / WebGL2 浏览器）；[wgpu 第三桌面后端](WGPU.md) 方案已定、代码未落地；[引擎 WASM + Web 查看器](WEB.md) 阶段 0/1 已落地
- **OCCT** 提供 BRep 几何内核
- **特征树 + 求值器** 做参数化编辑：改参数 → 重算 → 渲染

文档按 [路线图](ROADMAP.md) 里的分层排，不再平铺。

---

## 新手从这里开始

想学 **C++ 3D 开发**？把 Tamias 当成一部真实的三维软件来拆：先跑起来、再读骨架、再按兴趣深入。完整的入门教程在这里：

👉 [**Tamias 入门教程**](tutorial/index.md)

```
第 1 章 认识 Tamias ──────── 软件由哪些部件组成
第 2 章 构建与运行 ───────── 从源码编译并启动
第 3 章 界面与交互 ───────── 先当用户：放盒子、改参数
第 4 章 代码骨架 ★ ───────── main() 到一帧：分层与数据流
第 5 章 几何与造型 ───────── 三角网 / BRep / 特征树
第 6 章 文档与场景 ───────── Document / Scene / Entity
第 7 章 命令与撤销 ───────── 一切编辑都是命令
第 8 章 渲染管线 ★ ───────── 网格 → GPU → 像素
第 9 章 BIM 业务层 ───────── 建筑语义与门窗宿主
第 10 章 Web 与路线图 ────── WASM、wgpu、进阶路径
```

每章都写了「学什么 + 正文 + 动手练习 + 延伸阅读」，并直接链到下面的模块文档。看不懂模块文档时，先回到教程对应章节。

---

## 按模块读

```
┌────────────── Qt 客户端 (app) ──────────────┐
├────────────── 插件 / 脚本宿主 (plugin) ────────┤
├────────────── BIM 业务层 (bim) ──────────────┤
├────────────── 场景图 (document/scene) ────────┤
├────────────── 造型 (modeling) ───────────────┤
│  特征树 · 求值器 · 几何边界 (IShapeOps)        │
│  OCCT (STEP/IGES/BREP)  ·  IfcOpenShell (IFC)  │
├────────────── 渲染 (engine/render) ───────────┤
└───────────────────────────────────────────────┘
```

| 层 | 管什么 | 读这些 |
|---|---|---|
| **客户端** | 窗口、视口、属性面板、命令入口 | [Qt 壳](APP.md) |
| **插件** | C# 扩展：Ribbon 命令、只读查询、dispatch 内核命令 | [插件系列](plugin/index.md) |
| **BIM** | 楼层、轴网、墙梁板柱宿主、关联关系、当前标高 | [BIM 业务层](BIM.md)、[关联关系](bim/relations.md) |
| **场景图** | 语义树、变换、包围盒、展平 draw list | [总述](scene/index.md)、[语义树](SCENE-GRAPH.md) |
| **造型** | 特征树、求值、MCAD 深路径；内核插件口 | [特征树求值器](FEATURE-TREE-EVALUATOR.md)、[MCAD 管线](MCAD-PIPELINE.md)、[几何边界](ISHAPE-OPS.md) |
| **渲染** | 一帧怎么画、Vulkan/OpenGL/WebGL、屏外不画 | [管线与 RHI](RENDERING.md)、[视锥、NDC 与屏幕](NDC.md)、[OpenGL 后端](OPENGL.md)、[wgpu 接入](WGPU.md)、[Web 查看器](WEB.md)、[视锥剔除](FRUSTUM-CULLING.md) |

RHI 抽象写在 [渲染管线](RENDERING.md) 第 5 节；OpenGL 落地见 [OpenGL 后端](OPENGL.md)；第三后端方案见 [wgpu 接入](WGPU.md)。产品定位见 [MCAD 与 BIM](DECISION-MCAD-BIM.md)。
