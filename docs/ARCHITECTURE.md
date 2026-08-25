# Tamias 架构：引擎 / 宿主核心 / 胶水 / 界面

> 状态：**Phase 0–3 已落地，Phase 4 已文档化**。本文是四层架构的目标形态。

## 1. 分层总览

```
engine/             headless 核心：document / entity / command / undo / scene / render
                    （无 Qt、无平台窗口、无 UI）
    ↑ 稳定 C++ API
src/host/           Session + CameraController + 命令参数文本协议
                    （会话层：文档 + 命令系统 + 选择 + 工具模式 + 相机 + 事件）
    ↑ 每壳一个薄胶水
src/app/            Qt 适配：MainWindow / DocumentViewport 只保留 Qt 控件、事件转发、信号映射
src/wasm/           embind 适配：WebHost = Session 的 1:1 导出
src/plugin/         HostApi C ABI：能力面与 Session 对齐，实现读 Session 的文档/命令
    ↑
web/ (React + TS)   壳：工具栏、拖放、状态栏、键盘快捷键
```

两条硬规则：

- **`src/` 只放 C++**。非 C++ 工程（`web/`、`csharp/`）放根目录；
  **`plugins/`** 是插件示例区，按语言分子目录（如 `plugins/csharp/Tamias.Hello`），
  与系统组件（`csharp/Tamias.Api`、`csharp/Tamias.Host`）分开，用户一眼能看出插件放哪。
- **会话逻辑只写一份**。某段逻辑桌面和 web 都要用，就进 `src/host/`；只属于某一个壳的
  （QWidget、Ribbon、canvas、RHI 细节）留在壳里。

## 2. 为什么需要 `src/host/`

改造前，桌面壳 `DocumentViewport` 和 wasm 壳 `ViewerHost` 各自实现了几乎相同的宿主逻辑：

| 能力 | 桌面（改造前） | wasm（改造前） |
|---|---|---|
| Document 所有权 | DocumentViewport 持有 | ViewerHost 持有 |
| CommandSystem | DocumentViewport 持有（每文档一份） | 无（阶段 2 缺失） |
| 轨道相机 + 手势 | 自持 TurntableCamera，自写 mmb/pan | 自持 TurntableCamera，自写 pointer |
| 选择集 | 直接改 Document | 无 |
| 编辑命令（撤销） | 视口直连 command_system | 无 |

结果是：相机/输入/加载/刷新各写一份，且能力面漂移（web 没有命令系统、没有插件）。
`src/host/` 把这层抽出来，两个壳共享同一套会话语义。

## 3. `src/host/` 模块

### 3.1 Session（每文档一个）

```cpp
// src/host/session.h（示意）
class Session {
 public:
  explicit Session(std::shared_ptr<Document> document);

  Document& document();
  CommandSystem& command_system();
  CameraController& camera();

  ToolMode tool_mode() const;
  void set_tool(ToolMode mode);

  Result<void> dispatch(std::string_view name, const CommandArgs& args);
  void undo();  void redo();
  bool can_undo() const;  bool can_redo() const;

  // 选择（委托 Document）
  std::vector<std::uint64_t> selection() const;
  void select(std::uint64_t id);
  void deselect(std::uint64_t id);
  void clear_selection();
  void set_selection(const std::vector<std::uint64_t>& ids);

  // 事件（壳把 Session 事件翻译成 Qt 信号 / JS 回调）
  void set_listener(HostListener listener);
  void notify(HostEvent event, std::string_view message = {});
};
```

约定：

- **每文档一个 Session**，由持有它的壳适配器（桌面 `DocumentViewport`、wasm `ViewerHost`）拥有。
- Session **不 include Qt**，不碰 RenderChannel / canvas / 窗口。
- 渲染泵的节奏（Qt 事件循环 / requestAnimationFrame）留在壳里；Session 不驱动帧。
- 命令分发、撤销重做、选择、工具模式全部经 Session，保证两壳行为一致。

### 3.2 CameraController

统一轨道相机的手势参数与操作（orbit/pan 灵敏度、按距离缩放的 pan、聚焦缩放、框选全部），
两个壳只负责「哪个按钮进哪个手势」，数学和手感只存在一处。

### 3.3 命令参数文本协议（command_arg_text）

从 `src/plugin/` 移入 `src/host/`：`i:key=1;s:name=foo;v:vec=1,2,3` 文本 → `CommandArgs`。
插件 HostApi 与 wasm embind 共用同一套解析，协议不再各自为政。

## 4. 壳的职责（改薄后）

### 桌面 `src/app/`

- `MainWindow`：窗口、Ribbon、属性面板、状态栏、插件宿主（绑定到 `Session` 的 document/command）。
- `DocumentViewport`：Qt 控件 + 输入事件转发 + 覆盖层（视口立方体/工具条/框选/坐标读出）
  + 渲染表面管理；文档/命令/相机/工具/选择全部委托 `Session`。
- 刷新链路：Session 事件（DocumentChanged/SelectionChanged/ToolChanged）→ Qt 信号 →
  重绘 + 属性面板刷新。

### wasm `src/wasm/` + `web/`

- `ViewerHost`（瘦身为 WebHost）：`Session` + RenderThread + RenderChannel + canvas 管理。
- `viewer_main.cpp`：embind 导出 = Session 能力 1:1（load/resize/pointer/wheel/frameAll/
  renderFrame/status/documentName + dispatch/undo/redo/canUndo/canRedo/selection*）。
- `web/`：React + TS 壳，`src/viewer.ts` 类型声明与 embind 导出一一对应。

## 5. 插件对齐（Phase 4）

`HostApi`（C ABI）的能力面（entities/selection/dispatch/register_command）与 Session 高度重叠：

- 桌面插件宿主继续 `plugin_host_.bind(document*, command_system*, after_edit)`，
  传入的正是 Session 的 document/command_system；
- wasm 将来挂插件时，用同一个 HostApi + Session，接口与桌面一致。

## 6. 实施阶段

| 阶段 | 内容 | 验收 |
|---|---|---|
| 0 | ✅ `src/host/` 骨架：tool_mode、host_event、CameraController、Session 契约 | 两壳构建不回归 |
| 1 | ✅ CameraController 抽出来，桌面/Web 手势数学统一 | 手感逐项一致（Web 平移灵敏度对齐桌面 0.002） |
| 2 | ✅ Session 接管 Document/CommandSystem/选择/工具，桌面壳改薄 | 桌面构建通过，65/65 单测通过 |
| 3 | ✅ wasm 接 Session，`src/command` 进入 emscripten 构建，embind 扩面，TS 类型同步 | wasm 构建通过，web 可 dispatch/撤销/重做/选择 |
| 4 | ✅ 插件 HostApi 对齐 Session（文档化 + 绑定路径，`command_arg_text` 移入 host 共用） | 插件测试通过，行为不变 |

## 7. 关键文件

- `src/host/session.h/.cpp`、`camera_controller.h/.cpp`、`tool_mode.h`、`host_event.h`、`command_arg_text.h/.cpp`
- `src/app/document_viewport.*`、`src/app/main_window.cpp`
- `src/wasm/viewer_host.*`、`src/wasm/viewer_main.cpp`
- `web/src/viewer.ts`、`web/src/App.tsx`
