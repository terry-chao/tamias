# 第 4 章　代码骨架：从 main() 到一帧

> 本章目标（★ 骨架章节）：看懂 `src` 目录怎么分层、一条命令和一帧画面各走一条什么链路、新手在哪最容易迷路。

## 4.1 目录就是架构

```
src/
├── app/         Qt 壳：窗口、视口、面板、工具条（只发命令、只显示结果）
├── command/     命令系统：每个编辑操作 = 一个可撤销的 Command
├── plugin/      C# 插件宿主：HostApi C ABI、hostfxr、Ribbon「插件」页
├── entity/      参数化实体：BoxEntity、WallEntity、DoorEntity…（写特征树配方）
├── bim/         BIM 业务层：楼层、宿主、关联关系、IFC 空间结构
├── engine/
│   ├── core/    日志、结果类型、原生窗口句柄
│   ├── math/    向量/矩阵、相机、网格
│   ├── document/ Document（一个打开的文件）、Scene（语义树）、拾取
│   ├── graphics/ Mesh、GraphicsBackend 枚举
│   ├── io/      文件读写：binary_archive、mesh_io
│   ├── modeling/ 特征树、求值器、几何边界（IShapeOps / OCCT）
│   └── render/   渲染线程、RHI 抽象、Vulkan/OpenGL/WebGL 后端
├── web/         Web 查看器宿主（ViewerHost）
└── …（tests/ 在仓库根；plugin-sdk/csharp/ 是 C# 插件 SDK，plugins/ 是插件示例）
```

## 4.2 五层，每层只干一件事

```
┌────────────── Qt 客户端 (app) ──────────────┐
│  窗口 / 视口 / 工具按钮 / 属性面板 / 插件页    │
├────────────── 插件宿主 (plugin) ────────────┤
│  C# IPlugin · HostApi · dispatch 已有命令    │
├────────────── 命令 (command) ───────────────┤
│  事务、撤销；BIM 命令只调 bim 层              │
├────────────── BIM 业务层 (bim) ─────────────┤
│  关联关系 · 宿主更新 ·（将来）楼层/轴网        │
├────────────── 场景图 (document/scene) ───────┤
│  parent / 变换 / 包围盒（域无关容器）          │
├────────────── 造型 (modeling) ──────────────┤
│  特征树 · 求值器 · 几何边界 (IShapeOps)        │
├────────────── 渲染 (engine/render) ─────────┤
└─────────────────────────────────────────────┘
```

三条铁律，记住就不会迷路：

1. **app 不做几何，也不解释建筑规则**。它只发命令、打包一帧、显示结果。
2. **Scene 不解释建筑**。「这面墙在 2 楼」由 bim 层决定后写 `parent`，Scene 只记父子。
3. **渲染不认识「墙」**。它只收到「网格 + 世界矩阵 + 颜色」的平铺清单。

## 4.3 主线一：一条命令的数据流

以「属性面板把盒子深度改成 2.0」为例：

```
property_panel（改输入框）
  → SetFeatureParamCommand 构造并执行（src/command）
    → entity->model.set_param(..., "depth", 2.0)   只改配方
      → IGeometryBuilder.build(model) → OCCT 重算 → 新三角网
        → MeshAsset 换内容（同一个 mesh id）
          → 视口 resync_meshes → 上传新缓冲
            → 下一帧画面就是新形状
```

**关键点**：改的是「配方」里的一个数，不是直接改网格。所以能撤销、能重算、能存盘。

## 4.4 主线二：一帧画面的数据流

```
Scene（谁在哪）
  → Document::render_items() 展平
    → SceneDrawItem 列表（网格 + 世界矩阵 + 材质 + 选中）
      → 视口填 FrameSubmission（窗口、相机、清单）
        → RenderThread（渲染线程）
          → draw_channel 按顺序画：天空→网格→模型→轴→预览线
            → RHI（Vulkan / OpenGL）翻译成 GPU 调用
              → 窗口像素
```

中间那张「快递单」叫 **`SceneDrawItem`**：语义侧填好，渲染侧照着画，**渲染侧不知道这是一面墙**。

## 4.5 从 main() 开始（5 分钟读代码）

打开 [`src/app/main.cpp`](https://github.com/terry-chao/tamias/blob/main/src/app/main.cpp)，`main()` 只做四件事：

```cpp
QApplication app(argc, argv);          // 1. Qt 应用（AA_NativeWindows）
register_linked_rhi_backends();        // 2. 登记渲染后端（Vulkan/OpenGL…）
register_commands(command_registry()); // 3. 登记所有命令（盒子/墙/参数…）
register_occt_shape_ops();             // 4. 登记几何内核（STEP/IGES 读取）
MainWindow window; window.show();      // 5. 显示主窗口
```

后面所有功能都是这四样东西的排列组合：**命令 + 渲染后端 + 几何内核 + 窗口**。

## 4.6 动手练习

1. 在 `main.cpp` 里找到上面四个调用，读一遍周围代码，确认你理解每一行。
2. 打开 [`render_types.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/render/render_types.h)，找出 `SceneDrawItem` 有哪些字段。数一数：渲染侧是不是真的不知道「这是墙」？
3. 用调试器（或 `log`）在 `SetFeatureParamCommand::execute()` 断一次，走一遍 4.3 的链路。

## 延伸阅读

- [首页](../index.md)：分层表 + 每层读哪些文档
- [Qt 壳](../APP.md)：app 层职责与「现在有/还没有」
- [插件系列](../plugin/index.md)：C# 宿主与命令层的接缝
- [路线图](../ROADMAP.md) 第 2 节：分层架构 + 最容易误解的两点

下一章：[几何与造型](05-geometry-and-modeling.md)
