# qt

两套 UI 里的 Qt 那套：`tac` 契约的 Qt 6 实现。这是当前唯一允许 include Qt 的界面后端目录。

已实现：对话框、主题、文本度量（`PlatformServices`）。
待实现：`create_window` / `create_surface`（迁移计划阶段 3 / 4）。

上层不应直接 include 这里的头文件——只 include `tac/*`，由 `main()` 选择后端。
规则与迁移计划见 `docs/TAC.md`。
