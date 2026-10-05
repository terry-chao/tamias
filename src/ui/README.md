# ui

客户端界面层。两套实现共用 `tac` 的契约：

- `tac/` —— 自研界面库 tac：对外契约 + 将来的自研实现，**禁止 include Qt**
- `qt/` —— Qt 那套：同一份契约的 Qt 6 实现

`src/app/` 是**用界面**的壳（MainWindow / Ribbon / 视口 / 面板），不是界面库本身。

依赖方向：`app → tac ← qt`。规则与迁移计划见 `docs/TAC.md`，
门禁脚本 `scripts/check-tac-boundary.ps1`。
