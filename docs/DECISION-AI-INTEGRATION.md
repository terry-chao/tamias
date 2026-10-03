# AI 接入（MCP）：决策与方案

> 状态：**形态 B 已落地**（外部 stdio 进程 + 应用内桥）：协议门面 + Session 后端 +
> loopback 桥 + 发现文件 + `tamias-mcp` 转发器 + **三档写策略与审批** + 调用审计 +
> **内置 AI 对话面板** + 单测 41 例 + stdio / 面板两条端到端跑通。
> 剩余缺口见 [§5.2](#52-形态-b-还缺什么)。
> 目标是把 Tamias 的文档、命令与特征树交给
> AI/Agent 驱动，**不新开一条编辑通道**——写操作仍然只有 `dispatch` 一条路。
>
> 相关：[插件设计理念](plugin/design.md)（窄写宽读的来源）、
> [脚本与命令控制台](SCRIPTING.md)（全信任 C# 与审计回显）、
> [C ABI](plugin/api/abi.md)（稳定面）、[架构](ARCHITECTURE.md)（分层）。

## 1. 一句话结论

**在 `IHost` / `HostApi` 边界上做一个「能力门面」，实现一次，前端可换。**
AI 的每一次写操作都走 `dispatch` + 事务，用户按一次 `Ctrl+Z` 就能全退回。

不做的三件事：

- **不给 AI 开内核后门**：拿不到 `TopoDS_Shape`、RHI、特征树内部对象，和插件同一条边界。
- **不把 AI 塞进几何内核**：造型、渲染、撤销继续只被 C++ 拥有。
- **不发明第二套编辑 API**：命令注册表就是公共协议，AI 和工具条发的是同一条命令。

## 2. 为什么现有结构正好合适

Tamias 已经为「外部代码驱动文档」付过一遍成本了，接 AI 基本是复用：

| 已有资产 | 位置 | 对 AI 接入的意义 |
|---|---|---|
| `IHost` 窄契约（4 属性 + 11 方法） | [`Tamias.Api/IHost.cs`](https://github.com/terry-chao/tamias/blob/main/plugin-sdk/csharp/Tamias.Api/IHost.cs) | 现成的工具/资源边界，不用为 AI 另设 API |
| `HostApi` C ABI（v8，只追加） | [`src/plugin/host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h) | 稳定面；将来 Rust/Python 侧接入同一张表 |
| **窄写宽读**：写只有 `dispatch`，读有实体/特征/选择 | [`docs/plugin/design.md`](plugin/design.md) | 正好对应 MCP 的 tools（写）+ resources（读） |
| 事务 v7：一次求值 = 一条撤销 | [`docs/plugin/api/host.md`](plugin/api/host.md) | 一次 AI 动作 = 用户按一次 `Ctrl+Z` |
| 命令回显（`CommandObserver` → C# 文本） | [`src/host/command_echo.h`](https://github.com/terry-chao/tamias/blob/main/src/host/command_echo.h) | AI 的每次调用自动进控制台，**审计零成本** |
| 无头会话层 `Session`（不依赖 Qt） | [`src/host/session.h`](https://github.com/terry-chao/tamias/blob/main/src/host/session.h) | 桌面 / Web / 无头批处理共用同一份语义 |
| 离屏渲染 `capture_document_rgba` | [`src/engine/document/scene_capture.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/scene_capture.h) | 给多模态模型发视口截图 |
| 全信任脚本宿主 `CsharpRuntime::evaluate` | [`src/plugin/csharp_runtime.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.h) | B0 的捷径：一个工具立刻驱动全软件 |

## 3. 架构：门面 + 传输，两层分开

```
Claude Desktop / Cursor / VS Code / Codex              Tamias 内置 AI 面板（Qt dock）
        │ stdio (MCP)                                            │ HTTPS
        ▼                                                        ▼
 tamias-mcp（哑转发器：stdio → loopback）            OpenAiCompat / Anthropic provider
        │                                                        │ tool calls
        ▼                                                        ▼
 ┌───────────────── Tamias 进程 ──────────────────────────────────────────┐
 │  传输层：loopback HTTP/JSON-RPC（127.0.0.1，带 token，只绑本机）          │
 │        │                                                               │
 │  门面层：McpServer（JSON-RPC 2.0 + MCP 方法 + 工具注册表）                │
 │        │                                                               │
 │  后端层：SessionMcpBackend ──► Session ──► Dispatch / 事务 / 选择         │
 │                             └─► Document ──► 实体 / 特征树（宽读）        │
 │  审计：命令回显 → ConsolePanel（Ctrl+Shift+J）                           │
 └────────────────────────────────────────────────────────────────────────┘
```

**门面只实现一次**（工具描述符 + JSON 结果 + 后端接口），传输层可替换。
否则 stdio、HTTP、内置面板三份实现会各自漂移——那正是 `src/host/` 当初要消除的问题。

## 4. 能力面：resources + tools

### 4.1 Resources（宽读，喂上下文）

| URI | 内容 | 来源 |
|---|---|---|
| `tamias://document` | 名称、实体数、按 kind 统计、包围盒 | `DocumentName` / `Entities` |
| `tamias://entities` | 分页实体表 `(id, kind, name)` | `Entities` |
| `tamias://selection` | 当前选中 id（按选择顺序） | `Selection` |
| `tamias://entity/{id}/features` | 特征树 + 参数 + 依赖边 | `Features(id)` |
| `tamias://commands` | 命令目录 + 参数说明 | 见 [§9](#9-已决定--待拍板) |
| `tamias://view` | 当前视口 PNG（base64） | `capture_document_rgba` |

约定：**返回摘要 + 分页，不整表倾倒**。10 万实体的全量列表既烧 token 又不提高正确率；
`tamias://document` 先给统计，模型需要细节再翻页。

### 4.2 Tools（写，统一走 `dispatch`）

| Tool | 映射 / 语义 |
|---|---|
| `tamias_document_info` | 读：名称、实体数、统计 |
| `tamias_list_entities` | 读：`kind` / 名字过滤 + 分页 |
| `tamias_get_selection` | 读：当前选择 |
| `tamias_get_features` | 读：实体特征树与参数 |
| `tamias_dispatch` | **通用写**：`command` + 结构化 `args` → 命令文本 |
| `tamias_set_param` | 便捷写：`set_param` |
| `tamias_create_wall` | 便捷写：`create_wall`（点可缺省，见 §6） |
| `tamias_move_entities` | 便捷写：`move_entities` |
| `tamias_delete_entity` | 便捷写：`delete_entity` |
| `tamias_set_selection` | 只改选择，不动文档 |
| `tamias_batch` | 一组操作 = **一个事务** = 一条撤销 |
| `tamias_undo` / `tamias_redo` | 撤销 / 重做 |
| `tamias_evaluate` | **B0 逃生舱**：直接求值 C#（全信任）。**默认不在工具表里**，要 `--mcp-allow-evaluate` 打开 |

前几个高频命令做强类型包装（JSON Schema，模型不用猜参数文本编码）；
其余一律走 `tamias_dispatch`，能力面与命令注册表 1:1，不会漏。

写工具的内部实现固定三步：**开事务 → `dispatch` → `Commit`**。
失败 `Abort` 回滚，文档回到调用前的样子，撤销栈不留记录——和插件事务语义一致。

## 5. 运行形态：B —— 外部 stdio 进程 + 应用内桥

> **2026-10-03 变更**：主路径从「客户端直连进程内 loopback HTTP」改为
> **外部 stdio 进程 + 应用内桥**（形态 B，业界 C# 插件生态最普遍的形状）。
> 应用侧的 loopback HTTP **不废弃**——它从「客户端入口」降级为**桥的 IPC**。

| 形态 | 前端 | 生命周期 | 状态 |
|---|---|---|---|
| **B. 外部 stdio 进程 + 应用内桥** | Claude Desktop / Cursor / VS Code / Codex | 客户端拉起转发器，转发器连回已开的 Tamias | ⭐ **主路径（已定）**；转发器待实现 |
| A. 客户端直连 loopback HTTP | 支持 Streamable HTTP 的客户端 | 跟随 Tamias 窗口 | ✅ 已实现并端到端验证，保留为「不装转发器」时的直连方式 |
| 无头批量 MCP | CI / 批量 Agent | 自己开 `.tdoc`，不入 GUI | 待做 |
| 内置 AI 对话面板 | 不装第二个软件的用户 | 跟随 Tamias 窗口 | ✅ 已落地（v1） |

```
Claude Desktop ──stdio──> tamias-mcp（.NET 控制台，待实现）
                              │ HTTP + Bearer（IPC，已实现）
                              ▼
                    Tamias 进程内的 loopback 桥
                              ▼
                    McpServer 门面 → SessionMcpBackend → dispatch / 事务
```

**为什么 IPC 继续用 HTTP 而不是命名管道**：桥已经写好并验证过，换管道只换来「不开端口」，
而 token + 只绑回环 + Origin 检查已经覆盖当前威胁模型。真要做 ACL 隔离时再换，代价很小。

**选 B 不需要 ABI v9 的 `post_to_ui`。** 应用侧仍然是 C++（`QTcpServer` 天然在 UI 线程），
所以「主线程投递」这个原语可以晚点再加——它是「把桥搬进 C# 插件」的前提，不是 B 的前提。
这一点也是 B 相对「C# 插件内嵌服务器」的主要优势。

发现机制不变：Tamias 启动桥后写 `%APPDATA%/tamias/tamias/mcp.json`
（端口 + token + 文档名 + pid），转发器读它来找到活动实例。

### 5.1 应用内桥（已实现）

`--mcp` 现在开的是**桥**：对支持 Streamable HTTP 的客户端可以直接连，对只吃 stdio 的
客户端则由转发器连它。

```powershell
# 打开一个文档并开启桥；不指定端口就自动挑一个。默认只读。
& .\build\bin\Debug\tamias.exe --mcp .\model.obj

# 让 AI 能改文档：每次写操作弹确认框（推荐）/ 直接放行
& .\build\bin\Debug\tamias.exe --mcp --mcp-policy=ask .\model.obj
& .\build\bin\Debug\tamias.exe --mcp --mcp-policy=auto --mcp-port=52847 .\model.obj

# --mcp-allow-evaluate 额外打开全信任 C# 逃生舱（默认关闭，同样受写策略约束）
& .\build\bin\Debug\tamias.exe --mcp --mcp-policy=ask --mcp-allow-evaluate .\model.obj
```

**写策略（`--mcp-policy`，默认 `read-only`）**：

| 档 | 行为 |
|---|---|
| `read-only` | 只放读工具；写工具直接拒绝，并把原因回给模型 |
| `ask` | 写工具执行前在 Tamias 弹一次确认框，默认按钮是「否」 |
| `auto` | 写工具直接执行 |

闸门装在协议层（`McpServer` 的 tool gate），所以和传输无关：stdio、循环 HTTP、
以后的内置面板走的是同一道闸。每次工具调用（含被拒的）都会在命令控制台留一行
`[MCP] 工具名 | 文档 | 参数 | 成功/失败`。

启动后 `%APPDATA%/tamias/tamias/mcp.json` 里是：

```json
{"pid":12345,"port":52847,"token":"…","endpoint":"http://127.0.0.1:52847/mcp",
 "transport":"streamable-http","document":"model.obj","startedAt":"…"}
```

| 端点 | 方法 | 行为 |
|---|---|---|
| `/mcp` | POST | JSON-RPC 2.0 / MCP；通知回 `202` 无体 |
| `/mcp` | GET | `405`（本服务不主动推送，没有 SSE 流） |
| `/health` | GET | `{"ok":true,"document":"…"}`，用来探活 |

约定：只绑 `127.0.0.1`；除 `/health` 外所有请求都要带
`Authorization: Bearer <token>`（或 `X-Tamias-Token`）；带 `Origin` 的请求必须是
本机来源（DNS rebinding 防护）。请求处理跑在 Qt 事件循环上，也就是 **UI 线程**，
所以能直接读 Session——代价是长命令会卡界面，和控制台脚本求值同一性质。

坑：进程被强杀时 `mcp.json` 会残留。客户端应先探 `/health`，或核对 `pid` 是否还活着。

给 AI 客户端粘的配置（`tamias-mcp.exe` 和 `tamias.exe` 同目录，CMake 构建时自动发布；
桥启动时也会把这行打进命令控制台）：

```json
{"mcpServers":{"tamias":{"command":"C:\\dev\\tamias\\build\\bin\\Debug\\tamias-mcp.exe","args":[]}}}
```

Claude Desktop 写进 `claude_desktop_config.json`，Cursor 写进 `.cursor/mcp.json`，
重启客户端后在 Tamias 里用 `--mcp` 打开桥即可。

### 5.2 形态 B 还缺什么

**已落地（2026-10-03）——「B 能不能用」+「写操作能不能管住」：**

| # | 能力 | 落点 |
|---|---|---|
| 1 ✅ | 外部 stdio 转发器 `tamias-mcp`（哑转发，不含工具语义） | `plugin-sdk/csharp/Tamias.Mcp/` |
| 2 ✅ | 客户端配置形状（stdio 的 `command`/`args`） | `McpService::client_config_json()`，桥启动时打进控制台 |
| 3 ✅ | 打包分发 | `cmake/TamiasDotnet.cmake` publish 到 `tamias.exe` 同目录 |
| 4 ✅ | 发现文件存活判定与自愈 | 转发器：pid 存活 + `/health` 探活 + 失败重读，只重试一次 |
| 7 ✅ | 写策略三档 + 人工审批 | `--mcp-policy`；闸门装在 `mcp::McpServer` 的 tool gate 上，与传输无关 |
| 8 ✅ | MCP 调用审计 | 每次工具调用（含被拒）进命令控制台，带工具名/文档/参数/结果 |
| 9a ✅ | 执行期 `busy` 拒绝并发 | 审批框起嵌套事件循环时，另一个请求拿到 JSON-RPC error 而不是重入 |

**还缺：**

| # | 缺口 | 现状 | 不做的后果 | 落点 |
|---|---|---|---|---|
| 5 | **多实例语义** | 后启动的实例覆盖 `mcp.json` | 转发器连到哪个实例不确定 | 改 `mcp.d/<pid>.json` 由转发器挑活的；或明确「只支持一个 MCP 实例」 |
| 6 | **目标文档语义** | 工具打在**当前活动视口**上 | 用户在对话中途切页签 → 工具静默改到另一个文档 | 每个结果回显 document 标识 + 加 `tamias_list_documents`（~50 行，C++） |
| 9b | **长调用阻塞 UI** | 工具仍在 UI 线程同步执行 | 大 batch 会把界面冻住 | 长任务挪到线程 + 进度/取消 |
| 10 | **策略的持久化与 UI** | 策略只走命令行 | 每次都要记得加参数；没有界面可看/改 | AppSettings + 设置页 + 状态栏指示灯 |

第 6 项在 A 形态下就存在，只是 B 的长驻客户端把「用户中途切页签」放大成了常态。
转发器目前也没有单测/CI 接入，只有手跑的 `build/verify-mcp-stdio.ps1`。

### 5.3 内置 AI 对话面板（v1，已落地）

**视图 → 面板 → AI Assistant**，或 `Ctrl+Shift+A`。右侧停靠，默认收起。

和外部客户端走**同一套工具语义**，但不经 MCP 传输：面板直接调 `SessionMcpBackend`，
所以没有发现文件、端口、token 这些概念，也不需要 `--mcp`。

| | 外部客户端（B） | 内置面板 |
|---|---|---|
| 传输 | stdio → `tamias-mcp` → loopback 桥 | 进程内直接调用 |
| 适用 | Claude Desktop / Cursor / Codex | 不想装第二个软件 |
| 写策略闸门 | 受 `--mcp-policy` 约束 | **不受**：人在自己敲，和命令控制台同一性质 |
| 需要 `--mcp` | 是 | 否 |

配置（面板右上角「设置」）：任何 **OpenAI 兼容**端点都行——
`https://api.openai.com/v1`、`https://api.deepseek.com/v1`、
`http://127.0.0.1:11434/v1`（Ollama）、LM Studio、llama.cpp server。
服务地址和模型名会记住；**API key 只在本次会话的内存里，不落盘**。

一轮对话最多 8 轮工具调用（防止模型在「读-想-读」里转圈）。创建类命令如果返回
`armed: true`，表示工具已架起、等用户在视口点位置——这是设计好的两段式，见 §6。

调试用入口：`tamias.exe --ai-prompt="…"` 会打开面板并把这句话发出去（等文档就绪后再发）。
`build/verify-ai-panel.ps1` 用假的 OpenAI 兼容服务端把这条回路端到端跑一遍。

## 6. AI 规划、人点、AI 执行

创建类命令在点没给齐时**只武装工具、不执行、不进撤销栈**（见[命令与参数](plugin/api/commands.md#2-交互式还是一步到位)）。
这条现成语义正好解决「AI 不知道世界坐标」：

```
AI 调 tamias_create_wall(thickness=0.2, height=3)   不给 points
   → 工具被架起，返回 armed: waiting_for_user_input
   → 用户在视口点两个点
   → 命令真正执行，进撤销栈 + 控制台长出一行等价 C#
```

比让模型凭截图猜坐标靠谱得多。**点和实体拾取永远由人完成**，AI 只负责参数与顺序。

## 7. 安全模型

分三档，首次连接的默认值是**只读**：

| 档位 | 行为 |
|---|---|
| `read-only` | 只暴露读工具；写工具调用直接拒绝 |
| `ask` | 写操作先返回差异预览，用户在 Tamias 面板里点确认才提交 |
| `auto` | 白名单内的命令直接执行（BIM 编辑）；白名单外仍需确认 |

硬性约束：

1. **写只走 `dispatch`**，AI 拿不到内核对象。
2. **一调用一事务**，`tamias_batch` 合成一条撤销记录。
3. **命令白/黑名单**：默认禁掉一切涉及文件导入导出、打开保存的命令。
4. **只绑 `127.0.0.1`**，HTTP 头部带 token；UI 上有「AI 已连接」指示灯。
5. **审计**：每次调用进 ConsolePanel，与手点按钮共用同一份回显。
6. **凭据**：外部客户端模式下 Tamias 不持密钥；内置面板把密钥存 Windows 凭据管理器。
7. **`tamias_evaluate` 默认关闭**：它等于任意代码执行，只在明确信任的会话里开。

## 8. 分期实施

| 阶段 | 内容 | 验收 |
|---|---|---|
| **B0** ✅ | `tamias_evaluate` 逃生舱（走 `CsharpRuntime::evaluate`，`--mcp-allow-evaluate` 打开） | 客户端连上后 `host.DocumentName` 有输出；不开时工具表里没有它 |
| **B1** ✅ | `src/mcp/` 门面 + `SessionMcpBackend` + 单元测试 | `tools/list` / `tools/call` / `resources/*` 正确；假后端与真会话后端共 36 例通过 |
| **B2** ✅ | loopback HTTP 传输 + `--mcp` / `--mcp-port` / `--mcp-allow-evaluate` + 发现文件 + `after_edit` 刷新 | 端到端：握手、13 个工具、读文档、AI 建墙、撤销一步、无 token `401`、GET `405` 全部符合预期 |
| **B2.5** ✅ | 外部 stdio 转发器 `tamias-mcp` + 客户端配置形状 + 打包 + 发现文件存活/自愈 | stdio 端到端：握手、工具调用、Tamias 换端口重启后自愈、Tamias 未运行时返回可操作错误 |
| **B3a** ✅ | 三档写策略 + 人工审批 + 审计接入 ConsolePanel + 执行期 `busy` 拒绝并发 | 只读档写操作被拒并说明原因；auto 档放行；每次调用在控制台留痕 |
| **B3b** ⬅ **下一步** | 目标文档语义（结果回显 document + `tamias_list_documents`）+ 多实例语义 + 策略持久化/UI | 切页签不再静默换目标；两个 Tamias 并存时客户端知道连的是谁 |
| **B4a** ✅ | 内置 AI 对话面板：OpenAI 兼容端点 + 工具循环 + 设置（密钥不落盘） | 端到端：提问 → 模型要工具 → 面板执行 → 回填 → 最终答案；写操作真建出墙 |
| **B4b** | 流式输出 + Anthropic + 会话历史持久化 + 多轮上下文压缩 | 长回答不"卡住"；能换 provider |
| **B5** | 上下文质量：docs/IFC schema RAG、截图多模态、无头批量、web/wasm | 批量与保密场景可用；RAG 细化见 [RAG 接入](DECISION-RAG.md) |

**当前进度：B1、B2、B2.5 已完成（形态 A 的应用侧就是形态 B 的桥），下一步 B3。** 已有代码：

| 文件 | 作用 |
|---|---|
| `src/mcp/json.{h,cpp}` | 极简 JSON（解析 + 序列化），零第三方依赖 |
| `src/mcp/mcp_server.{h,cpp}` | JSON-RPC 2.0 信封 + MCP 方法（initialize / tools / resources / templates） |
| `src/mcp/mcp_backend.h` | `McpBackend` 接口 + JSON Schema 构造小工具 |
| `src/host/command_arg_json.{h,cpp}` | 结构化 JSON 参数 → 命令参数文本协议 |
| `src/host/session_mcp_backend.{h,cpp}` | `Session` → 13 个工具 + 4 个资源 + 1 个资源模板 |
| `src/app/mcp/mcp_http_server.{h,cpp}` | 手写极简 HTTP/1.1（QTcpServer，只绑回环，keep-alive） |
| `src/app/mcp/mcp_service.{h,cpp}` | 组门面 + 后端 + 传输 + 发现文件 + 鉴权；跟活动文档重绑 |
| `src/app/mcp/mcp_policy.h` | 三档写策略（read-only / ask / auto）与命令行解析 |
| `src/app/ai/ai_client.{h,cpp}` | OpenAI 兼容 `/chat/completions` 客户端（工具调用、mcp::Json ↔ Qt JSON） |
| `src/app/shell/panel/ai_panel.{h,cpp}` | 对话面板：转录 + 输入 + 工具循环；跟活动文档重绑 |
| `src/app/shell/main_window.cpp` | `bind_plugin_session` 里同步重绑 MCP；启动/停止入口 `set_mcp_enabled` |
| `plugin-sdk/csharp/Tamias.Mcp/` | 外部 stdio 转发器：发现文件 + pid/`health` 校验 + 失败重读重试 |
| `tests/mcp_server_tests.cpp`、`tests/mcp_session_backend_tests.cpp` | 协议层 21 例、真会话后端 17 例 |

## 9. 已决定 / 待拍板

**已决定：**

- 门面挂在 `IHost`/`HostApi` 边界，**不新开编辑通道**；写只走 `dispatch`。
- 门面实现一次，传输可换；**桌面主路径 = 外部 stdio 进程 + 应用内 loopback 桥（形态 B）**。
- 桥的 IPC 保留 loopback HTTP + token（不换命名管道）；进 C# 插件所需的 `post_to_ui` 原语不在 B 的前提里。
- 写操作一调用一事务；`batch` 合并为一条撤销记录。
- 点/实体拾取由人完成，AI 只调参；两段式武装是标准交互。
- 默认只读；写操作按 `ask` 档需人工确认。

**待拍板：**

- **命令目录（`tamias://commands`）的来源**：构建期从 [`register_commands.cpp`](https://github.com/terry-chao/tamias/blob/main/src/command/core/register_commands.cpp)
  生成，还是新增只读 ABI（v9）在运行时枚举？倾向**构建期生成**——零 ABI 改动，且能把参数说明一起带上。
- **`tamias_evaluate` 的长期定位**：永久保留为逃生舱，还是只作为 B0 验证后下线？
- **无头批量的写回策略**：直接改 `.tdoc` 落盘，还是产出补丁脚本？

## 10. 代码落点

| 层 | 位置 | 说明 |
|---|---|---|
| 协议核心 | `src/mcp/`（`json` / `mcp_server` / `mcp_backend`） | 纯 C++，无 Qt、无 engine；假后端可单测 |
| 会话后端 | `src/host/session_mcp_backend.{h,cpp}` | `Session` → 工具；与桌面/Web 共用 |
| 应用内桥 | `src/app/mcp/` | Qt 壳：`QTcpServer` loopback（请求天然在 UI 线程）+ 发现文件 + `--mcp` 开关 |
| 外部 stdio 转发器 | `plugin-sdk/csharp/Tamias.Mcp/`（待建） | stdio ↔ HTTP 哑转发，便于 Claude Desktop 一行配置 |
| 内置面板 | `src/app/shell/panel/ai_panel.{h,cpp}` | 参照 [`console_panel`](https://github.com/terry-chao/tamias/blob/main/src/app/shell/panel/console_panel.cpp) |
| 测试 | `tests/mcp_*_tests.cpp` | 协议与工具语义 |

## 11. 和现有文档的关系

- 插件 / 脚本是**人写的扩展**，AI 是**模型发起的扩展**——同一个 `IHost`，同一条命令线。
- 控制台里的「全信任 C#」边界不变；AI 工具是它之上的一层**更窄、可审计、带审批**的入口。
- 几何内核、特征树求值、RHI 都不进这条链路，见 [路线图 §6](ROADMAP.md)。
