# tac：自研界面库与 Qt 后端

> 状态：**Phase 0 已落地**（目录 / 目标 / 契约 / Qt 那套的平台服务）。本文是目标形态与迁移计划。

目标形态是**两套 UI，共用一份契约**：

- **Qt 那套**：`src/ui/qt/`，今天在跑的界面。
- **tac 那套**：`src/ui/tac/`，自研界面库。当前先落抽象契约与平台服务接口，
  自研控件 / 绘制实现后续长在同一个目录里，最终成为默认后端。

## 1. 为什么要有这一层

目标是让 Tamias 不再把 Qt 当成"唯一界面库"，而是把 Qt 降级成**两套实现之一**，
另一套是自研的 `tac`。

现状（扫描结论）：

- `engine/` `host/` `command/` `mcp/` `rag/` **完全无 Qt**；桌面扫描出的 2 处
  "命中"其实是 `<queue>`，不是 Qt。
- Qt 只出现在 `src/app/`：143 个 cpp/h 里 131 个含 Qt 头。
- 渲染侧早已用 `engine/base/native_window_handle.h` 的 `NativeWindowHandle`
  与 Qt 解耦：`RenderChannel` 只吃裸句柄。
- 插件侧已有一套平台服务接口的雏形：`plugin/host_api.h` 的 `show_dialog` +
  C# `Tamias.Api.IUi`。

所以抽象面在 `src/app` **内部**，`engine/` 与 `host/` 一行都不用动。

## 2. 目录与目标

| 路径 | CMake 目标 | 角色 |
|---|---|---|
| `src/ui/tac/` | `tamias::tac` | **自研界面库 tac**：对外契约 + 将来的自研实现。**禁止 include Qt** |
| `src/ui/qt/` | `tamias::qt` | **Qt 那套**：同一份契约的 Qt 6 实现。Qt 只允许出现在这里 |
| `src/app/` | `tamias` | 桌面壳。迁移期内仍可直接用 Qt，但白名单只减不增 |

依赖方向：

```
tamias (src/app) ──▶ tamias::tac（契约）◀── tamias::qt   Qt 那套
                                        ◀── tamias::tac      自研那套（长在 src/ui/tac 内）
```

`qt` 依赖 `tac`，不反过来。上层只 include `ui/tac/*`，两套在 `main()` 里选。

## 3. 契约清单

| 头文件 | 内容 | Qt 后端状态 |
|---|---|---|
| `tac/core/types.h` | `Point` / `Size` / `Rect` / `Margins` / `Color` | 值类型，无需后端 |
| `tac/core/input.h` | `Key` / `Modifier` / `MouseButton` / 鼠标·滚轮·键盘·缩放事件 | 阶段 4 接事件 |
| `tac/platform/dialogs.h` | `DialogService`：消息框 / 单字段 / 表单 / 打开·保存文件 | ✅ 已实现 |
| `tac/platform/theme.h` | `Theme` + `ThemePalette` | ✅ 已实现 |
| `tac/platform/text.h` | `TextMeasurer` + `FontSpec` / `FontMetrics` | ✅ 已实现 |
| `tac/platform/window.h` | `Window` / `WindowDesc`（`native_handle()`） | 阶段 3 |
| `tac/surface.h` | `ViewportSurface` + `SurfaceEventSink` | 阶段 4 |
| `tac/shell/commands.h` | `CommandInfo` / `Shortcut` | 阶段 2 |
| `tac/shell/spec.h` | `ShellSpec` / `MenuSpec` / `RibbonPageSpec` / `DockSpec` | 阶段 2 |
| `tac/backend.h` | `UiBackend` / `BackendInfo` / 后端登记表 | ✅ 登记就位 |

对话框枚举（`DialogButtons` / `DialogResult` / `PromptFieldKind`）与 C# 侧
`Tamias.Api` 数值一一对应，插件 API 不变。

## 4. 边界规则

1. `src/ui/tac/**` 不得 include 任何 Qt 头。
2. 只有 `src/ui/qt/**` 允许 Qt；`src/app/**` 是迁移期白名单，**只减不增**。
3. `src/app` 新代码不要直接 `QMessageBox` / `QFileDialog` / `QInputDialog` /
   `QSettings`，走 `PlatformServices`。
4. 事件循环与帧节奏留在后端，`Session` 不碰窗口（沿用 `ARCHITECTURE.md` 的约定）。
5. 插件侧 API 不动：`HostApi.show_dialog` 在 C++ 侧路由到 `tac::DialogService`。

门禁脚本：

```powershell
pwsh -File scripts/check-tac-boundary.ps1
```

它做三件事：`src/ui/tac` 无 Qt 硬失败；`src/app` Qt 文件数超过基线失败；
`src/app` 与 `src/ui/qt` 之外出现 Qt 失败。迁移掉文件后，把脚本里的
`$LegacyAppQtBaseline` 调小。

## 5. 迁移阶段

| 阶段 | 内容 | 验收 | 状态 |
|---|---|---|---|
| 0 | 建 `tac` / `qt`：目录、CMake 目标、契约、Qt 那套的平台服务、门禁脚本 | 构建不回归；门禁通过 | ✅ |
| 1 | 平台服务全量走接口：设置 / 主题 / i18n / 文件框 / 消息框 / Toast / 通用对话框 | `src/app` 直接使用 `QSettings` / `QFileDialog` / `QMessageBox` 归零；基线下调 | |
| 2 | 菜单 / 工具带声明式化：`CommandInfo` + `ShellSpec`，`RibbonBar` 变成渲染器 | `main_window.cpp` 显著缩小；插件按钮路径不变 | |
| 3 | 面板改跑 Widget 接口；`UiBackend::create_window` 落地 | 每个面板可独立切后端 | |
| 4 | 视口拆分：`ViewportController`（无 Qt）+ `ViewportSurface`（Qt 实现） | 渲染无回归；surface 只暴露 `NativeWindowHandle` | |
| 5 | 在 `src/ui/tac/` 内落自研控件 / 绘制实现，后端名 `tac`，运行期切换 | 自研那套通过 parity checklist，Qt 降为可选 | |

阶段 4 最难（`document_viewport.cpp` 约 151 KB），放最后。阶段 1、2 性价比最高，先做。

## 6. 怎么选后端

默认 **qt**，改了要重启（和渲染后端、建模内核同一条规矩）：

| 方式 | 做法 |
|---|---|
| 设置界面 | 设置 → Interface → Toolkit → UI backend |
| 配置文件 | 设置文件里的 `ui/backend` 键（`<exe 目录>/config/tamias.ini`，见 [app 文档](APP.md#设置落盘在哪儿)） |
| 命令行 | `tamias --ui-backend=tac`，临时覆盖、不写回设置 |

启动时用 `tac::create_backend_or_fallback(requested, "qt", &used)` 落地：请求的后端
没登记就回退到 qt，并打一条 `log_warn`。所以就算配置里写了 `tac` 而 `tac`
还没落地，程序照常启动、只是回退到 qt。设置界面只列出**登记过**的后端。

`tac` 现在已登记（`ui/tac/native_backend.cpp` 的 `tac::register_native_backend()`），
所以下拉框里能选中它；但那是个**骨架后端**：自研控件 / 绘制还没落地，平台服务返回
「不支持」，`create_window` / `create_surface` 返回 `nullptr`。阶段 5 把自研实现长进
`src/ui/tac/` 后，替换 `native_backend.cpp` 里的工厂即可，设置项不用动。

```cpp
#include "ui/tac/backend.h"
#include "ui/qt/backend.h"

tac::qt::register_qt_backend();  // 自研那套落地后加 tac::register_native_backend()
tac::register_native_backend();  // 登记 "tac"（当前是骨架，界面仍是 Qt）
std::string used;
auto backend = tac::create_backend_or_fallback("qt", "qt", &used);
if (backend) {
  auto& services = backend->platform();
  services.dialogs().show_message("提示", "后端就绪");
}
```

注意：**这个开关现在还不改变界面**。壳（`src/app`）仍旧是 Qt 代码，选项只是把
选择定下来、记日志；等阶段 1–4 把 `app` 迁到 `tac` 接口上，它才会真正切换界面。

## 7. 与现有模块的关系

- **渲染**：`ViewportSurface::native_handle()` 直接返回 `NativeWindowHandle`，
  喂给现有的 `RenderChannel::resize`，渲染线程不需要知道后端是谁。
- **插件**：`HostApi.show_dialog` 的实现从 `app/shell/dialog/plugin_prompt_dialog.*`
  切到 `tac::DialogService`，插件二进制与 C# 侧零改动。
- **wasm**：`src/ui/tac` 无 Qt，wasm 分支也构建它；`src/ui/qt` 只在桌面构建。
- **TacUI**：自研那套的实现库，两条来源二选一，target 名一样（`TacUI::tacui_host` /
  `TacUI::tacui`），都记在 `TAMIAS_TACUI_TARGET` 里：
  - `TAMIAS_TACUI_SOURCE_DIR` 指向本地 checkout（`msvc` preset 默认 `C:/dev/TacUI`）——
    当子工程编，改 TacUI 源码下次 `cmake --build` 就生效，不用重装；
  - 留空 —— vcpkg 按 `vcpkg.json` 装的那份（`vcpkg-configuration.json` 指向 TacUI
    自己的 git registry，依赖限定 `windows & x64`）。

  Linux / wasm 不解析这个依赖。现在还没有目标链它，所以本地那份是 `EXCLUDE_FROM_ALL`、不占
  编译时间；阶段 5 把自研实现长进 `src/ui/tac/` 时接上。见 [BUILD.md](../BUILD.md) 的
  TacUI 一节。
