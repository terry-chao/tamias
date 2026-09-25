# Qt 客户端（app）

> 路线图分层里最上面：**窗口、视口、面板、工具**。几何真相不在这里，app 只发命令、打包一帧、显示结果。墙梁板柱的**归属 / 楼层 / 轴网**也不在这里，见 [BIM 业务层](BIM.md)。

代码在 [`src/app/`](https://github.com/terry-chao/tamias/tree/main/src/app)。命令与实体紧贴这一层，但不属于 Qt：[`src/command/`](https://github.com/terry-chao/tamias/tree/main/src/command)、[`src/entity/`](https://github.com/terry-chao/tamias/tree/main/src/entity)。BIM 命令落地后应调 `src/bim`，而不是在窗口里写宿主规则。

目录按职责分组（只留 `main.cpp`、`qt_pch.h`、`resources.qrc`、`app.rc` 在 `src/app/` 根下）：

```
src/app/
├── base/      应用基础设施：app_settings、theme、i18n、recent_files、qt_path.h、rhi_backends.cpp
├── shell/     窗口骨架：main_window、main_menu（顶栏菜单）、home_page、ribbon_*、toast、
│              mesh_thumbnail、设置/关于/插件对话框
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
├── drawing/   参考图纸：drawing_document、drawing_view、drawing_manager_panel、
│              drawing_settings_dialog、drawing_import_dialog
├── texture/   贴图：texture_image、texture_library_panel、texture_inspector_dialog
└── debug/     调试诊断：handle_inspector、render_scene_inspector、scene_debugger_window、timing_panel、timing_timeline_widget、golden_test_runner、pin_result_dialog
```

BIM 业务层的分组只影响「谁在管哪个面板」，不放宽上面那条：`bim/` 里的面板照样只发命令、只显示结果。

---

## 干什么

| 职责 | 谁 |
|---|---|
| 主窗口、多标签 | `main_window` |
| 顶栏菜单（文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助） | `main_menu` |
| 工具带（分区、图标 / 文字两态、分组拖出成浮动小工具栏） | `ribbon_bar` / `ribbon_page` / `ribbon_group` / `ribbon_float_window` |
| 欢迎页、最近打开 | `home_page` / `recent_files` |
| 三维视口、相机、点选、提交帧 | `document_viewport` |
| 句柄检查（Ctrl+D） | `handle_inspector` |
| ViewCube | `view_cube_widget` |
| 属性面板（改特征参数） | `property_panel` |
| 视口工具列（视口右侧通高，左列按钮 + 右侧功能页） | `viewport_tool_panel` |
| 命令控制台（底部停靠，Ctrl+Shift+J；回显 + C# 求值） | `console_panel` |
| 构件显隐页（按类别显隐，Ctrl+L） | `visibility_panel` / `entity_kind_catalog` |
| 楼层面板（按楼层显隐 + 当前楼层，Ctrl+Shift+L） | `floor_panel` / `viewport_floor.h` |
| 图纸管理页（视口右列；把 DWF/DXF/PDF 等图纸挂在文档下，显隐 / 摆放 / 定位） | `drawing_manager_panel` / `drawing_settings_dialog` |
| 图纸底图（贴进三维视口的贴图平面 + 测试深度不写深度） | `document_viewport`（`sync_drawing_underlays`） / `render_runtime`（`overlay_pipeline_`） |
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

## 顶栏：菜单栏 + Ribbon

窗口最上面一行是**菜单栏**（文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助，见
`shell/main_menu.cpp`），排法照 FreeCAD 与主流 CAD 走；品牌小图标和「切换形态 / 卷起」
两个小按钮挂在它的左右两端（`QMenuBar` 的角部 widget），所以这一行就是顶栏本身，
下面直接接工具带、没有第二条横栏。

- **文件**：新建 / 打开 / 打开图纸 / 最近打开（动态子菜单，文件没了会灰掉但仍列出来）/
  保存 / 另存为 / 导出渲染场景快照 / 关闭标签页（`Ctrl+W`）/ 退出。
- **编辑**：撤销 / 重做 + 通用编辑（移动 / 复制 / 旋转 / 镜像 / 阵列）+ 设置
  （偏好设置在「编辑」下，和 FreeCAD 的 Edit → Preferences 一致）。
- **视图**：显示模式（线框 / 着色 / 真实感）、X 光、全部框显、轴网与轴网设置、参考图纸、
  标注、**面板**（停靠面板与视口右侧工具列都在这里开关）、回欢迎页。
- **工具**：插件管理，以及调试这一帧 / 钉进测试这类排障动作。
- **窗口**：下一个 / 上一个标签页（`Ctrl+Tab` / `Ctrl+Shift+Tab`）+ **打开的文档清单**
  （当前那个打勾，点一下就切过去），每次弹出前重建。
- **帮助**：在线手册、图形诊断、关于 Tamias。

菜单项和功能区**共用同一批 QAction**（`main_window` 里建一次），所以改名、换图标、
勾选态、快捷键都只有一份。没有打开文档时，只对文档有意义的命令（保存 / 撤销 / 重做 /
通用编辑 / 全部框显 / 视口面板入口…）会一起灰掉——这是 FreeCAD / Office 的做法；
构件工具（墙 / 线 / 注释…）有意留着可点，点了会给一句「先打开一个模型」的提示。
Alt+F 这类助记符照常可用：菜单栏虽然挂在 Ribbon 上（不是 `QMainWindow::menuBar()`），
Qt 的助记符一样生效。

### 一条工具带，两段分区（没有页签、也没有工具栏那行）

菜单栏那行就是**顶上唯一的一行**：文件 / 编辑 / 视图 / 工具 / 窗口 / 帮助，
最右边两个小按钮（切换形态、卷起）——挂在菜单栏自己的角部 widget 上，不另占高度
（品牌 logo 只在欢迎页、关于框和窗口图标上用，菜单栏不摆）。
菜单下面直接就是整条工具带：**开始 / 视图 两「页」同时铺开**，没有页签，也没有要切过去
才看得见的工具，更没有再挂一排「新建 / 打开 / 保存 / 撤销 / 重做」的小图标（新建 / 打开 /
保存 在「文件」组里本来就有，**撤销 / 重做 进了「文件」组右边的「编辑」组**）。
两页在代码里仍是两个 `RibbonPage`（`home` / `view`，布局记忆与插件落点还按这两个 id 认），
只是页签没了以后它们不再互相遮挡，而是上下接成一条带子上的两段**分区**：

```
文件 编辑 视图 工具 窗口 帮助                    [样式][⌃]   ← 菜单行（唯一的一行）
▌ 文件 编辑 绘制 建筑 结构 修改 注释 …      ← 「开始」这一段（蓝线）
─┼─────────────────────────────────────
▌ 显示 标注 面板 工作区                     ← 「视图」这一段（青绿线）
```

- **怎么认分区**：每段最左边一条 **3px 主色竖线**，贯穿这一段的全部排；两段之间一条
  横线。线色按段的先后取（蓝 / 青绿 / 橙 / 紫，循环），深色主题自动换成亮一档的同一组色。
  **段名不画在带上**（开始 / 视图 这种名字看菜单也知道，画出来白占地方）：鼠标停在竖线上，
  提示里给「段名 + 双击卷起」。
- **每段要几排由它自己决定**：拖动可以给一段加排（最多 6 排），排空的排自动收起、
  后面的排往上顶；段高 = 可见排数 × 单排高（92 / 仅图标 54），两段的排数互不影响，
  整条带子的高度是两段相加。
- **宽度不够**：那一段自己出横向滚动条；滚轮在带上滚就是左右滚（不用按 Shift）。
  列不下时也可以切「仅图标」——1600 宽下两段各一排、都不再滚，整条带子 141px
  （「图标 + 文字」这时是 229px，开始那一段还得横向滚：36 个按钮 × 每个至少 52px）。
- **卷起 / 展开**：菜单栏右上角的小箭头，或者**双击分区竖线 / 菜单栏空白处**，只留菜单那一行。

两种形态，右上角按钮与**设置 → 界面 → 功能区**都能切，选择记在设置里（`ui/ribbon_style`）：

| 形态 | 长什么样 | 何时用 |
|---|---|---|
| 图标 + 文字（默认） | 每个工具一张 28px 图标 + 名字，分组下面还有组名 | 教学、不熟快捷键时 |
| 仅图标（`ui/ribbon_style=icons`） | 只有图标，鼠标悬浮出名字（FreeCAD 那种），整条带子也更矮 | 熟练用户，省纵向空间 |

**分组可以拖出来**（SketchUp 那种浮动小工具栏）：

- 每个分组**左边**有一条竖抓手（三个小点，`⋮`）。按住它拖出去，这一组就浮成一个小窗：
  标题栏可以继续拖着走，`×` 收回去。抓手只是视觉提示——按分组身上任何非按钮处都能拖。
- 拖到工具带上会显示一条插入线，松手就按落点停靠回去（一段之内左右换位置，也可以换排）；
  只在它原来那一段停靠，跨段搬组会把「组属于哪一页」的登记弄乱。
- **拖过就记下来**：分组的排 / 排内先后（`ui/ribbon_layout`，每条 `page|group|row|index`）、
  哪几组浮着 + 浮窗位置（`ui/ribbon_floating_groups`）、卷起状态（`ui/ribbon_collapsed`）
  都进设置，下次开还是你摆的样子（形态 `ui/ribbon_style` 同理）。浮窗被拖着挪位置时按
  400ms 节流存一次，不会每挪一像素就写盘。
  旧记录对不上的条目直接忽略；记录里没有的分组（新版本新增、插件后加的）补在末尾，
  不会因为一份旧布局而消失。想回到出厂排布：工具带右侧的样式按钮 →「重置功能区布局」。

实现都在 `shell/ribbon_*`：`ribbon_bar`（工具行、分区主色与顺序、形态、拖放、浮窗管理）、
`ribbon_page`（一段分区的排布局、分区标题栏与色标、分组的停靠 / 摘出、落点线）、
`ribbon_group`（按钮、抓手与分区色标，拖拽源）、`ribbon_float_window`
（浮起来的那一小条）。拖拽走 Qt 自己的 drag & drop：`RibbonGroup` 把 `page_id|group_id`
塞进私有 MIME 类型 `application/x-tamias-ribbon-group`，`RibbonBar` 兼作落点。

---

## 插件

Ribbon「开始 → 插件」由 `PluginHost` 在启动时加载 C# 插件。插件只读选择、然后 `Dispatch` 已有命令，不碰 OCCT / 场景图 / RHI。找不到 nethost 时主程序照常开，只是没有插件命令。

完整说明：[插件系列](plugin/index.md)（理念、使用、宿主功能、开发）。

## 命令控制台

底部停靠面板（**视图 → 面板 → 命令控制台**，`Ctrl+Shift+J`），上下两半：

**输出面**：每条真正执行的内核命令长一行等价的 C# 调用（`host.Dispatch("create_wall", …)`）。在工具条上画一面墙，这里就有可抄走的代码——学 API 不必先读文档。文本由 [command_echo.h](https://github.com/terry-chao/tamias/blob/main/src/host/command_echo.h) 生成，插件 `Log` 也落在同一个面板。

**输入面**：敲一段 C#，`Ctrl+Enter` 跑。`host` 就是当前文档的宿主（`IHost`），能读特征树、能 `Dispatch`、能弹宿主对话框。

它同时是个**文件型脚本页**（见 [console_panel](https://github.com/terry-chao/tamias/blob/main/src/app/shell/console_panel.cpp)）：

- 带行号的编辑器，上下拖动分隔条调「回显 / 编辑器」比例。
- 脚本住在 `<AppData>/scripts`（`Ctrl+S` 保存；第一次保存直接落进这个目录，不弹框）。**不进 `.tdoc`**——脚本是行为，工作文档是数据；脚本应该能用 git 管、能拷给同事。
- 下拉框列出脚本目录里的 `.cs`，换脚本前有未保存改动会先问一句（标题上带 `*`）。
- 求值失败时，错误文本里的 `(行,列)` 会把光标直接带到出错那行（Roslyn 的诊断格式）。

两条要说清楚的：

- **全信任，不是沙箱。** 脚本在 Tamias 进程里跑，拿到的是 `IHost` 的全部能力——和 FreeCAD 的 Python 控制台一个性质：给操作者自己用的工具。
- **每段求值 = 一个事务。** 改错了按一次 `Ctrl+Z` 全部退回。所以脚本里**不要**自己再开事务（不支持嵌套）。

求值在托管侧的 `Tamias.Host`（Roslyn scripting），所以需要 .NET 运行时；没有时控制台只回显、不能求值，其余功能照常。代价是 `managed/` 多了约 10 MB 的 Roslyn 程序集。

---

## 现在有 / 还没有

**有：** 打开 `.tdoc` / `.trscn` / 导入网格、转相机、点选、挤出等特征的属性编辑、墙工具预览线、线框/着色/真实模式、**开始 → 轴网设置**（按间距生成正交轴网 / 逐根增删改名，确定后在视口里**点击放置**）、**视图 → 渲染场景**（`Ctrl+Shift+I`：对照 draw list、写 `.trscn` / 钉金样 / 导出 OBJ；`Ctrl+Shift+P` 钉进 `assets/samples/render/`，调试步骤见 [渲染场景快照](RENDER-SCENE.md#调试步骤)）、**Ctrl+D 句柄检查窗口**（点选构件显示 `.tdoc` 里的 id、所在楼层）、**构件显隐页**（`Ctrl+L`，按类别一键显隐）、**楼层面板**（`Ctrl+Shift+L`，一层一行勾选显隐 + 选当前楼层，齿轮开「楼层设置」改标高 / 层高 / 夹层）、**楼层管理页**（视口右列，一张楼层视图清单，第一行「全局三维」= 默认视图；双击某层就是打开该层的视图：只留这一层、设为当前楼层、切到平面并框到该层；在这张视图里切回三维就是**该层的三维**——只留这一层的过滤照旧，双击第一行或「全部显示」才回到全局三维）、**开始 → 插件**（C# 示例：列出选择 / 删除所选）、**设置 → Modeling → Kernel backend**（选建模内核：OCCT 完整 / Truck 实验性，重启生效，见 [建模内核](MODELING-KERNEL.md)）。

**命令控制台**（`Ctrl+Shift+J`）：输出面把每条执行过的内核命令渲染成可抄走的 C# 调用，
输入面能直接跑 C# 片段（每段一个事务），脚本存在 `<AppData>/scripts`。见[脚本与命令控制台](SCRIPTING.md)。

**还没有（路线图支撑线）：** 大纲树、测量、工作台切换、完整建模草图 UI。壳继续长在 app 里；BIM 规则走 [BIM 业务层](BIM.md)，内核仍是 command →（bim）→ document → modeling。
