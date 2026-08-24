# Tamias

跨 MCAD / BIM 的几何查看 + 参数化编辑内核。

![Tamias 软件截图](docs/image.png)

## 快速开始

```powershell
cmake --preset msvc
cmake --build --preset relwithdebinfo --parallel
& .\build\bin\RelWithDebInfo\tamias.exe
```

前置依赖与常见问题见 [BUILD.md](BUILD.md)；切换浏览器（WASM）预设见 [WEB.md](docs/WEB.md)。

## 文档

- [**入门教程**](docs/tutorial/index.md) —— 把 Tamias 当教学样例，带你入门 C++ 3D 开发（10 章 + 术语表）
- [总览与路线图](docs/ROADMAP.md) —— 定位、分层架构、里程碑 P1–P4
- 模块文档 —— 按 [客户端](docs/APP.md) / [BIM](docs/BIM.md) / [场景图](docs/SCENE-GRAPH.md) / [造型](docs/FEATURE-TREE-EVALUATOR.md) / [渲染](docs/RENDERING.md) 分类

在线站点：https://terry-chao.github.io/tamias/
