# Qt 客户端（app）

> 路线图分层里最上面：**窗口、视口、面板、工具**。几何真相不在这里，app 只发命令、打包一帧、显示结果。墙梁板柱的**归属 / 楼层 / 轴网**也不在这里，见 [BIM 业务层](BIM.md)。

代码在 [`src/app/`](https://github.com/terry-chao/tamias/tree/main/src/app)。命令与实体紧贴这一层，但不属于 Qt：[`src/command/`](https://github.com/terry-chao/tamias/tree/main/src/command)、[`src/entity/`](https://github.com/terry-chao/tamias/tree/main/src/entity)。BIM 命令落地后应调 `src/bim`，而不是在窗口里写宿主规则。

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
| 楼层设置对话框（标高 / 层高 / 夹层） | `floor_settings_dialog` / `update_storeys_command` |
| 设置（含选 Vulkan / OpenGL） | `app_settings` / `settings_dialog` |
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

**有：** 打开 `.tdoc` / `.trscn` / 导入网格、转相机、点选、挤出等特征的属性编辑、墙工具预览线、线框/着色/真实模式、**视图 → 渲染场景**（`Ctrl+Shift+I`：对照 draw list、写 `.trscn` / 钉金样 / 导出 OBJ；`Ctrl+Shift+P` 钉进 `assets/samples/render/`，调试步骤见 [渲染场景快照](RENDER-SCENE.md#调试步骤)）、**Ctrl+D 句柄检查窗口**（点选构件显示 `.tdoc` 里的 id）、**构件显隐页**（`Ctrl+L`，按类别一键显隐）、**楼层面板**（`Ctrl+Shift+L`，一层一行勾选显隐 + 选当前楼层，齿轮开「楼层设置」改标高 / 层高 / 夹层）、**开始 → 插件**（C# 示例：列出选择 / 删除所选）。

**还没有（路线图支撑线）：** 大纲树、测量、工作台切换、完整建模草图 UI、轴网 UI。壳继续长在 app 里；BIM 规则走 [BIM 业务层](BIM.md)，内核仍是 command →（bim）→ document → modeling。
