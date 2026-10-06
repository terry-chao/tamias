# tac

自研界面库 `tac`。当前先落抽象契约；自研控件 / 绘制实现后续长在这个目录里。
上层（`src/app`）只依赖这里；Qt 那套在 `src/ui/qt`，本目录禁止 include Qt。

- `core/` 值类型与输入事件
- `platform/` 平台服务：对话框、主题、文本度量、窗口
- `shell/` 声明式外壳模型：命令元数据、菜单 / 工具带 / 停靠面板描述
- `surface.h` 渲染画布：只要拿得出 `NativeWindowHandle` 就能挂渲染线程
- `backend.h` `UiBackend` 契约与后端登记表
- `native_backend.cpp` `tac` 后端骨架（登记到设置里可选，自研控件待补）

规则与迁移计划见 `docs/TAC.md`。
