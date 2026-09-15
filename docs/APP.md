# Qt 客户端（app）

> 路线图分层里最上面：**窗口、视口、面板、工具**。几何真相不在这里，app 只发命令、打包一帧、显示结果。墙梁板柱的**归属 / 楼层 / 轴网**也不在这里，见 [BIM 业务层](BIM.md)。

代码在 [`src/app/`](https://github.com/terry-chao/tamias/tree/main/src/app)。命令与实体紧贴这一层，但不属于 Qt：[`src/command/`](https://github.com/terry-chao/tamias/tree/main/src/command)、[`src/entity/`](https://github.com/terry-chao/tamias/tree/main/src/entity)。BIM 命令落地后应调 `src/bim`，而不是在窗口里写宿主规则。

目录按职责分组（只留 `main.cpp`、`qt_pch.h`、`resources.qrc`、`app.rc` 在 `src/app/` 根下）：

```
src/app/
├── base/      应用基础设施：app_settings、theme、i18n、recent_files、qt_path.h、rhi_backends.cpp
├── shell/     窗口骨架：main_window、home_page、ribbon_*、toast、mesh_thumbnail、设置/关于/插件对话框
├── viewport/  三维视口，按部件再分一层：
│   ├── canvas/  画布本体（document_viewport、viewport_floor.h：相机、点选、提交帧、楼层带）
│   ├── overlay/ 画布上的浮层（view_cube_widget 朝向立方体、box_select_overlay 框选）
│   ├── panel/   视口右侧工具列（viewport_tool_panel）
│   └── replay/  渲染场景录制回放（replay_viewport）
├── bim/       构件界面，按功能域再分一层：
│   ├── properties/ 属性面板（改特征参数）
│   ├── components/ 绘制面板 + 构件规格 + 截面预览（draw_panel、component_specs、param_spec、section_preview_*）
│   ├── visibility/ 构件显隐页 + 类别目录（visibility_panel、entity_kind_catalog）
│   ├── floors/     楼层面板 / 楼层管理页 / 楼层设置（floor_*）
│   └── grid/       轴网设置（grid_settings_dialog）
├── drawing/   二维图纸：drawing_document、drawing_view、drawing_manager_panel、drawing_import_dialog
├── texture/   贴图：texture_image、texture_library_panel、texture_inspector_dialog
└── debug/     调试诊断：handle_inspector、render_scene_inspector、scene_debugger_window、timing_panel、timing_timeline_widget、golden_test_runner、pin_result_dialog
```

BIM 业务层的分组只影响「谁在管哪个面板」，不放宽上面那条：`bim/` 里的面板照样只发命令、只显示结果。

---

## 干什么

| 职责 | 谁 |
|---|---|
| 主窗口、多标签、菜单 | `main_window` |
| 欢迎页、最近打开 | `home_page` / `recent_files` |
| 三维视口、相机、点选、提交帧 | `document_viewport` |
| 句柄检查（Ctrl+D） | `handle_inspector` |
| ViewCube | `view_cube_widget` |
| 属性面板（改特征参数） | `property_panel` |
| 视口工具列（视口右侧通高，左列按钮 + 右侧功能页） | `viewport_tool_panel` |
| 构件显隐页（按类别显隐，Ctrl+L） | `visibility_panel` / `entity_kind_catalog` |
| 楼层面板（按楼层显隐 + 当前楼层，Ctrl+Shift+L） | `floor_panel` / `viewport_floor.h` |
| 图纸管理页（视口右列；把 DWF/DXF/PDF 等图纸挂在文档下，双击打开） | `drawing_manager_panel` / `drawing_document` |
| 楼层设置对话框（标高 / 层高 / 夹层） | `floor_settings_dialog` / `update_storeys_command` |
| 楼层管理页（楼层视图清单：全局三维 + 每层，双击打开） | `floor_manager_panel` |
| 设置（选 Vulkan / OpenGL，选建模内核 OCCT / Truck） | `app_settings` / `settings_dialog` |
| 登记 RHI 后端 | `rhi_backends.cpp` |

和下面几层的接缝：

```
属性面板改参数
  → Command（src/command）
    → BIM 构件：BimModel（楼层 / 宿主）→ Document
    → MCAD 实体：直接 Document
      → modeling 重算 → 新网格
        → DocumentViewport upload + render_items()
          → 渲染线程画一帧
```

视口怎么喊「画一帧」见 [渲染管线](RENDERING.md) 第 3 节。

---

## 插件

Ribbon「开始 → 插件」由 `PluginHost` 在启动时加载 C# 插件。插件只读选择、然后 `Dispatch` 已有命令，不碰 OCCT / 场景图 / RHI。找不到 nethost 时主程序照常开，只是没有插件命令。

完整说明：[插件系列](plugin/index.md)（理念、使用、宿主功能、开发）。

---

## 现在有 / 还没有

**有：** 打开 `.tdoc` / `.trscn` / 导入网格、转相机、点选、挤出等特征的属性编辑、墙工具预览线、线框/着色/真实模式、**开始 → 轴网设置**（按间距生成正交轴网 / 逐根增删改名，确定后在视口里**点击放置**）、**视图 → 渲染场景**（`Ctrl+Shift+I`：对照 draw list、写 `.trscn` / 钉金样 / 导出 OBJ；`Ctrl+Shift+P` 钉进 `assets/samples/render/`，调试步骤见 [渲染场景快照](RENDER-SCENE.md#调试步骤)）、**Ctrl+D 句柄检查窗口**（点选构件显示 `.tdoc` 里的 id）、**构件显隐页**（`Ctrl+L`，按类别一键显隐）、**楼层面板**（`Ctrl+Shift+L`，一层一行勾选显隐 + 选当前楼层，齿轮开「楼层设置」改标高 / 层高 / 夹层）、**楼层管理页**（视口右列，一张楼层视图清单，第一行「全局三维」= 默认视图；双击某层就是打开该层的平面视图：只留这一层、设为当前楼层、切到平面并框到该层；切回三维或「全部显示」即回到全局三维）、**开始 → 插件**（C# 示例：列出选择 / 删除所选）、**设置 → Modeling → Kernel backend**（选建模内核：OCCT 完整 / Truck 实验性，重启生效，见 [建模内核](MODELING-KERNEL.md)）。

**还没有（路线图支撑线）：** 大纲树、测量、工作台切换、完整建模草图 UI。壳继续长在 app 里；BIM 规则走 [BIM 业务层](BIM.md)，内核仍是 command →（bim）→ document → modeling。
