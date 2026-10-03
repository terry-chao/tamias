# AI

把文档、命令与特征树交给 AI / Agent 驱动。**不新开编辑通道**：AI 和工具条发的是
同一条命令，写只走 `dispatch` + 事务，用户按一次 `Ctrl+Z` 就能全退回。

- [AI 接入（MCP）](../DECISION-AI-INTEGRATION.md) —— 能力门面 + 可换传输：resources / tools、
  三档写策略、人工审批与审计、内置 AI 面板。
- [RAG 接入](../DECISION-RAG.md) —— 文档检索挂在同一门面的读侧：语料、切块、BM25、
  引用与拒答；不碰写路径。

## 入口形态

| 入口 | 传输 | 状态 |
|---|---|---|
| 内置 AI 面板（`Ctrl+Shift+A`） | 进程内直调 `SessionMcpBackend`，无需 `--mcp` | ✅ v1 已落地 |
| 外部 MCP 客户端（Claude Desktop / Cursor / VS Code / Codex） | stdio → `tamias-mcp` → loopback 桥 | ✅ 已落地 |
| 直连 loopback HTTP | 支持 Streamable HTTP 的客户端 | ✅ 已落地 |
| 无头批量 | CI / 批量 Agent，自己开 `.tdoc`，不进 GUI | 待做 |

三种入口共用同一份工具语义与写策略闸门（内置面板例外：人在自己敲，不受 `--mcp-policy` 约束）。

## 边界

- **不给 AI 开内核后门**：拿不到 `TopoDS_Shape`、RHI、特征树内部对象，和插件同一条 `IHost` 边界。
- **不发明第二套编辑 API**：命令注册表就是公共协议，能力面与它 1:1。
- **点和实体拾取永远由人完成**：AI 只负责参数与顺序，创建类命令支持「先武装、再点位置」。
- **默认只读**：写操作按 `read-only` / `ask` / `auto` 三档管理，每次调用进命令控制台留痕。
