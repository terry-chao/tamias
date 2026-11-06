# 节点建模（Node Modeling）

> 状态：P1 已落地（数据层 / 求值 / 预览 / 烘焙 / .tdoc 持久化 / 底部编辑器面板）。
> 参考实现：[CHMOSE023/MiniCAD](https://github.com/CHMOSE023/MiniCAD) 的 `src/NodeGraph/`
> （其设计文档在 `docs/NodeGraph/PLAN.md`）；本文档描述 tamias 侧的取舍与差异。

## 一图流

```
节点编辑器面板（底部 dock）
   │  编辑（全部经命令 → 文档撤销栈，Ctrl+Z 统一）
   ▼
Document::node_graph()          ← 每个文档一张图，随 .tdoc 的 Node chunk 存取
   │  求值（30ms 防抖 → 预算内同步跑；pull + 脏标记 + Kahn 拓扑序）
   ▼
Body 输出（MKI 不可变共享体）
   │  预览收集（眼睛开着的节点）→ tessellate(0.05)
   ▼
Document::set_node_preview()    ← 橙色半透明叠加进视口（不进 Scene、不可选、不存档）
   │  烘焙（node_bake 命令，一条 undo）
   ▼
普通网格节点（同 import_mesh 的哑几何路径；参数化烘焙是 P3）
```

## 分层与文件

| 层 | 文件 | 说明 |
|---|---|---|
| 数据 | `src/engine/modeling/nodes/node_value.h` | 端口/参数的值（variant；几何是 `BodyRef` 不可变共享体） |
| 数据 | `nodes/node_graph.h/.cpp` | `Node`/`Link`/`Graph` 纯数据图；`connect` 拒绝成环；Kahn 拓扑序；脏标记带 stamp（P2 后台求值回填校验用） |
| 类型 | `nodes/node_type.h/.cpp` | `NodeTypeInfo` 聚合 + `solve` 函数 + 注册表；模糊搜索与端口兼容度搜索共用一套打分 |
| 求值 | `nodes/node_eval.h/.cpp` | `evaluate_graph`：只解脏节点，跳过干净节点（复用 outputs）；错误→节点 Error、下游 Warning |
| 内置节点 | `nodes/builtin/*.cpp` | P1 九个：`input.number`、`solid.box/cylinder/polygon_extrude/boolean/fillet/chamfer`、`xform.move`、`output.preview`——全部映射到既有 MKI 动词 |
| 序列化 | `nodes/node_io.h/.cpp` | 只存结构与标量（几何是求值结果，加载后全量重算）；端口/参数按**名字**匹配；未知类型 = 丢失节点保数据 |
| 文档 | `engine/document/document.*` | 图持有（懒创建）+ 预览条目 API + `render_items()` 末尾追加预览 draw item |
| 命令 | `src/command/nodes/graph_edit_commands.*` | 图编辑七连（增删节点 / 连拔线 / 改值改参数 / 移动），全走 CommandStack |
| 命令 | `src/command/nodes/bake_graph_command.*` | 烘焙：强制算完 → `add_import_mesh` 哑几何，一条 undo |
| UI | `src/app/shell/panel/node_editor_panel.*` | dock 面板：求值调度（30ms 防抖 + 150ms 预算门）、预览同步（Body 指针身份缓存细分）、250ms 轮询捕捉 undo/切换/加载 |
| UI | `panel/node_canvas.*` | 全 QPainter 自绘画布：卡片 / 贝塞尔连线 / 端口拖拽 / scrub / 内联编辑 / 框选 |
| UI | `panel/node_palette_popup.*` | 搜索面板；从端口拖出时按兼容度排序 + 自动连最佳端口 |

## 与 MiniCAD 的刻意差异

| 方面 | MiniCAD | tamias |
|---|---|---|
| 撤销 | 图独立 JSON 快照栈 | 走文档 CommandStack（统一 Ctrl+Z、插件/脚本免费获得） |
| 序列化端口匹配 | 按索引（插端口毁旧档） | 按名字 + 「新端口只追加」约定 |
| 内核 | `ISolidKernel` 直连 SMLib | `ModelKernel`（OCCT/Truck/SMLib 三后端，见 mki/kernel.h） |
| 求值调度 | 每帧 RenderFrame 同步入口 | Qt 事件驱动：编辑信号 → 防抖 QTimer；轮询签名捕捉外部变化 |
| 列表语义 | Grasshopper longest-list | P1 没有（一个输入端口至多一根入线），P2 再加 |
| 组合节点 / 表达式 / 参数表 | 有 | P3+ |

## 关键约定

- **图始终无环**：`Graph::connect` 六查（端点/端口/类型/占线/重复/成环），求值只走拓扑序，运行期不需要环检测。
- **几何不入档**：`.tdoc` 的 `Node` chunk 只存结构与标量；`Body` 是求值结果，加载后 `mark_all_dirty` 重算。
- **预览不是实体**：不进 Scene、不可选、不参与 BIM / 保存；渲染帧里按 `kNodePreviewNodeIdBase`（`1<<62` 起的哨兵段）追加 draw item。烘焙之前，预览改动对文档数据零影响。
- **一个输入端口至多一根入线**（P1）；多入拼接（列表）留 P2。
- **新节点默认开预览**：带 `Body` 输出的节点（创建 / 修改 / output）建出来眼睛就是亮的，加一个 Box 立刻能在视口看到橙色预览；数值类节点没有几何输出，不开。
- **求值不进撤销**：结构编辑可撤销，求值只是缓存——撤销后 250ms 轮询发现签名变化自动重算。

## 操作速查

| 操作 | 效果 |
|---|---|
| 右键空白 / 工具栏 Add node | 搜索面板加节点（实体节点建出来即预览） |
| 从端口拖到空白 | 搜索面板按兼容度过滤，选完**自动连最佳端口** |
| 从已连的输入端口拖 | 拔线重接（先断开，从上游输出继续拖） |
| Alt+点击连线 | 删那根线 |
| 横向拖数值字段 | scrub（0.1 步进；Shift 0.01 细调；Ctrl 整数吸附） |
| 双击字段 | 原位编辑（数字 / 文本 / "x,y,z" 向量） |
| 点击眼睛 | 开 / 关该节点的视口预览（橙色）；新建实体节点默认开 |
| F / Ctrl+A / Delete / Esc | 适配全图 / 全选 / 删除选中 / 清选 |
| 中键或右键拖 | 平移；滚轮缩放（对准光标） |
| Bake | 预览开着的节点 + output 节点 → 固化成普通网格（一条 Ctrl+Z 撤销）；烘完自动关预览并选中新实体 |

## 三维造型入门：造一个立方体

1. **加节点**：工具栏「添加节点」→ 搜 `box` → 选中 `Box`。卡片默认 1×1×1，底面落在 y = 0，
   在 XZ 平面上居中（`Width` → X、`Height` → Y 竖直方向、`Depth` → Z）。
2. **改尺寸**：横向拖三个数值字段（Shift 细调、Ctrl 整数吸附），或者双击字段直接输入。
   立方体 = 三个值设成同一个数，比如都填 `2`。
3. **看预览**：新节点默认开预览，视口里立刻出现橙色实体。橙色 = 节点预览，还不是文档实体，
   不进场景、不可选、不存档。
4. **固化成实体**：点 **Bake** → 预览体变成普通网格节点（可选中、参与 BIM、随文档保存）。
   烘焙后预览会自动关掉、新实体在视口里自动选中，所以橙色消失、模型就地出现（位置不变）。
   Bake 是快照：之后改节点参数不会回头改已烘焙的网格；要改就 Ctrl+Z 撤销烘焙再重来。
5. **继续造型**：`Boolean`（`op` 选 fuse / cut / common）、`Fillet` / `Chamfer`（`edges` 填边号，
   空 = 全部边）、`Move`（`Offset` 填 `x,y,z`）、`Polygon Extrude`（`Points` 填 `x,z;x,z;…`）、
   `Cylinder`。全是 Body → Body，从输出端口拖到输入端口的圆点（或那一行的数值框）就接上。

## 脚本 / 插件入口

注册表命令（文本协议同其它命令，见 host/command_arg_text.h）：
`node_add`、`node_delete`、`node_connect`、`node_disconnect`、`node_set_input`、
`node_set_param`、`node_bake`。示例：

```
node_add s:type=solid.box d:x=100 d:y=100
node_connect i:from=1 i:from_port=0 i:to=2 i:to_port=0
node_set_param i:node=3 i:param=0 s:value=cut
node_bake
```

## 路线

- **P2**：后台求值（CloneFull + stamp 回填 + 内核进程锁）、scrub 节流展示陈旧结果、
  剪贴板 / Ctrl+D、`doc.entity` 引用节点（以既有实体为输入，`Scene::generation` 门控）、
  曲线节点（line/circle/rect-wire，`lines=true` 线框预览）、`.tnode` 独立导出。
- **P3+**：子图编译回 FeatureModel 的参数化烘焙、组合节点（group 库）、表达式节点、
  列表 longest-list、节点类型插件 SDK（HostApi 声明式 schema）、web/embind 暴露。

## 测试

`tests/node_graph_tests.cpp`（图操作 / 成环 / 拓扑 / 脏传播）、`node_eval_tests.cpp`
（每节点求值 / 错误传播 / 脏节点跳过）、`node_io_tests.cpp`（往返 / 未知类型 / 版本拒绝 /
半截档）、`node_palette_search_tests.cpp`（打分 / 中文关键词 / 端口过滤）。
