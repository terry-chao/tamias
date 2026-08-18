# Tamias

跨 MCAD / BIM 的**几何查看 + 参数化编辑内核**。

- **Qt** 做壳，自研 RHI 双后端（Vulkan 主 / OpenGL 副）；[wgpu 第三后端](WGPU.md) 方案已定、代码未落地
- **OCCT** 提供 BRep 几何内核
- **特征树 + 求值器** 做参数化编辑：改参数 → 重算 → 渲染

文档按 [路线图](ROADMAP.md) 里的分层排，不再平铺。

---

## 按模块读

```
┌────────────── Qt 客户端 (app) ──────────────┐
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
| **BIM 业务** | 楼层、轴网、墙梁板柱宿主、当前标高 | [BIM 业务层](BIM.md) |
| **场景图** | 语义树、变换、包围盒、展平 draw list | [总述](scene/index.md)、[语义树](SCENE-GRAPH.md) |
| **造型** | 特征树、求值、MCAD 深路径；内核插件口 | [特征树求值器](FEATURE-TREE-EVALUATOR.md)、[MCAD 管线](MCAD-PIPELINE.md)、[几何边界](ISHAPE-OPS.md) |
| **渲染** | 一帧怎么画、Vulkan/OpenGL、屏外不画 | [管线与 RHI](RENDERING.md)、[视锥、NDC 与屏幕](NDC.md)、[OpenGL 后端](OPENGL.md)、[wgpu 接入](WGPU.md)、[视锥剔除](FRUSTUM-CULLING.md) |

RHI 抽象写在 [渲染管线](RENDERING.md) 第 5 节；OpenGL 落地见 [OpenGL 后端](OPENGL.md)；第三后端方案见 [wgpu 接入](WGPU.md)。产品定位见 [MCAD 与 BIM](DECISION-MCAD-BIM.md)。

---

## 本地怎么看

要起一个本地服务，**不要**双击生成出来的 `index.html`（主题、搜索、相对链接会坏）。

在仓库根目录：

```bash
pip install -r requirements.txt
mkdocs serve
```

浏览器打开 [http://127.0.0.1:8000](http://127.0.0.1:8000)。改 `docs/` 里的 Markdown 会自动刷新。停掉：终端里 `Ctrl+C`。

只生成静态站点、不起服务：

```bash
mkdocs build
```

产物在 `site/`。需要本地预览时仍建议用 `mkdocs serve`，或对 `site/` 再开一个静态文件服务。

推到 `main` 后，GitHub Actions 会把同一套站点发到 <https://terry-chao.github.io/tamias/>。

程序本身怎么编译运行，见 [BUILD.md](https://github.com/terry-chao/tamias/blob/main/BUILD.md)。
