# 第 3 章　界面与交互：先当用户，再当开发者

> 本章目标：把 Tamias 玩熟——知道每个界面元素背后是哪个代码模块，完成第一次建模（放一个盒子、拖一面墙、改一个参数）。先当用户，是理解软件最快的方式。

## 3.1 打开之后你看到什么

| 界面元素 | 干什么 | 背后的代码模块 |
|---|---|---|
| 主窗口 + 菜单 | 开文件、保存、设置 | [`main_window`](https://github.com/terry-chao/tamias/blob/main/src/app/main_window.cpp) |
| 欢迎页 | 新建/最近打开 | [`home_page`](https://github.com/terry-chao/tamias/blob/main/src/app/home_page.cpp)、[`recent_files`](https://github.com/terry-chao/tamias/blob/main/src/app/recent_files.cpp) |
| 三维视口 | 看模型、转相机、点选 | [`document_viewport`](https://github.com/terry-chao/tamias/blob/main/src/app/document_viewport.cpp) |
| 右侧工具条 | 选工具：盒子/墙/梁/板/柱/窗/门… | [`viewport_tool_strip`](https://github.com/terry-chao/tamias/blob/main/src/app/viewport_tool_strip.cpp) |
| 属性面板 | 改选中构件的参数 | [`property_panel`](https://github.com/terry-chao/tamias/blob/main/src/app/property_panel.cpp) |
| ViewCube | 快速换视角 | [`view_cube_widget`](https://github.com/terry-chao/tamias/blob/main/src/app/view_cube_widget.cpp) |
| 设置对话框 | 选渲染后端（Vulkan/OpenGL）等 | [`settings_dialog`](https://github.com/terry-chao/tamias/blob/main/src/app/settings_dialog.cpp) |
| 句柄检查（Ctrl+D） | 显示构件在 `.tdoc` 里的 id | [`handle_inspector`](https://github.com/terry-chao/tamias/blob/main/src/app/handle_inspector.cpp) |

## 3.2 相机操作（先记住这套）

| 操作 | 效果 |
|---|---|
| 左键拖拽 | 环绕旋转（orbit） |
| 左键单击（不拖） | 拾取 / 选中 |
| 右键 / 中键拖拽 | 平移（pan） |
| 滚轮 | 推拉（dolly） |
| `F` | 框住全部（frame all） |

相机是 **Y-up 转盘相机**：绕目标点转 yaw/pitch，和 Blender、glTF 惯例一致。实现见 [`camera.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/math/camera.h)。

## 3.3 第一次建模：点一下，它就出现了

1. 右侧工具条选「盒子」（或墙）。
2. 在网格上**点一下**（墙是点两下：起点→终点）。
3. 一个参数化实体出现在视口里。
4. 选中它，在属性面板改「深度/高度」→ 形状立刻更新。
5. 按 `F` 框住全部，转一圈看看。

这五步背后是一条完整链路，**第 5、7、8 章会各讲一段**：

```
点工具/点地面
  → Command 记下位置（src/command）
    → Entity 写特征树配方（src/entity）
      → OCCT 求值出 BRep → 三角网（src/engine/modeling）
        → 进 Document + SceneNode（src/engine/document）
          → 视口上传网格、提交一帧（src/app + src/engine/render）
```

## 3.4 试试打开模型文件

菜单里可以打开：

- `.tdoc`：Tamias 自己的工作文档（带参数化配方）
- `.obj` / `.glb`：通用三角网格
- `.step` / `.iges` / `.brep`：CAD 交换格式（OCCT 读入，变三角网格）
- `.ifc`：建筑交换格式（目前只读空间结构，几何还没导入）

`.tdoc` 和 `.obj` 的区别就是**第 5 章**的核心：一个记得「这是怎么造出来的」，另一个只记得「最终长什么样」。

## 3.5 动手练习

1. 放一个盒子，改 3 个不同参数，观察形状变化。
2. 拖一面墙，然后在墙上放一扇窗或门——注意它们会不会「贴」在墙上（这是 BIM 宿主关系，第 9 章讲）。
3. 按 `Ctrl+D` 打开句柄检查，点选几个构件，看看 id 是怎么分配的。
4. 试试设置里切换 Vulkan / OpenGL，用 `F` 框住后比较画面（行为应一致）。

## 延伸阅读

- [Qt 壳](../APP.md)：app 层职责与接缝
- [渲染管线](../RENDERING.md) 第 3 节：视口怎么喊「画一帧」
- [关联关系](../bim/relations.md)：窗/门贴墙的规则

下一章：[代码骨架](04-code-map.md)
