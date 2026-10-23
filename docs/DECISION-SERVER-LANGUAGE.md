# 服务端语言：决策与边界

> 状态：**已定案并落地骨架，端到端跑通**。`src/server` 的扁平 C ABI（22 个导出函数）
> + `server/csharp/Tamias.Server` 的 ASP.NET Core 壳；实测：ABI 握手通过、会话建立、
> 一批 3 条命令经 OCCT 内核建成 3 个实体、**一次 undo 整批退回**、未知命令 409、
> 不支持的文档类型 415、销毁会话后 404。
> 业务端点仍只有最小作业面；**未决事项见 [§8](#8-未决事项)**——先读那一节再当作承诺。
> 上手怎么跑看 [`server/csharp/README.md`](https://github.com/terry-chao/tamias/blob/main/server/csharp/README.md)。
>
> 结论是 **C++ 内核 + C#/.NET 服务壳**：几何的所有权留在 C++，服务壳只做传输与编排。
> 相关：[引擎 WASM + Web 查看器](WEB.md)（客户端那半边）、
> [AI 接入（MCP）](DECISION-AI-INTEGRATION.md)（同一个 Session 的另一条传输）、
> [架构](ARCHITECTURE.md)（分层）、[C ABI](plugin/api/abi.md)（插件面的稳定 ABI）。

---

## 1. 一句话结论

**服务端不是一个新的应用，是 `Session` 的第四个宿主。**
所以语言选择的实质只有一个：**谁来写那个壳**。

答案是 C++ 写内核、C#/.NET 写壳，理由是**几何的所有权不能分裂**：

- 几何的每一次求值都在 C++ 里（OCCT / Truck / 特征树 / 撤销栈），服务壳拿不到也不该拿到；
- 壳要做的事是「传输 + 编排 + 作业边界」——这些用 .NET 写效率高得多（ASP.NET Core、
  依赖注入、部署生态），而且**团队已经有 .NET 8 工具链**；
- 两者之间的边界必须是**粗粒度**的：一次调用做完一整件事，几何永远不过边界。

不做的三件事：

- **不让 .NET 去 bind OCCT / IfcOpenShell**：OCCT 没有官方 C# 绑定，IfcOpenShell 只有
  C++ / Python 绑定。几何只被 C++ 拥有，服务壳只发命令、收快照。
- **不为服务端发明第二套编辑 API**：写操作仍然只有 `dispatch`，走的是桌面 / 插件 / AI
  同一条命令注册表。新增的只是**传输层**的扁平 C ABI。
- **不把服务壳做成新的内核**：服务壳里不该出现任何几何类型、矩阵、容差判断。

## 2. 为什么现有结构正好合适

Tamias 已经为「一份内核、多个宿主」付过一遍成本了，加服务端基本是复用：

| 已有资产 | 位置 | 对服务端的意义 |
|---|---|---|
| 无头会话层 `Session`（**不 include Qt**） | [`src/host/session.h`](https://github.com/terry-chao/tamias/blob/main/src/host/session.h) | 服务端就是它的第四个宿主，不用重新设计会话 |
| 三个宿主已经共用它 | `src/app`(Qt) / `src/wasm`(embind) / `src/app/mcp`(HTTP) | 加第四个是既有模式的延续，不是特例 |
| 扁平 C ABI 的先例 | [`truck_bridge.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/modeling/mki/truck/truck_bridge.h) | 错误码 + `last_error()` 的约定直接照搬 |
| 版本化、只追加的 ABI 纪律 | [`src/plugin/host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)（v8） | 服务端 ABI 用同一套规矩（`TAMIAS_SERVER_ABI_VERSION`） |
| 命令参数文本解析器 | [`src/host/command_arg_text.h`](https://github.com/terry-chao/tamias/blob/main/src/host/command_arg_text.h) | 服务端收的参数格式和桌面控制台**逐字相同** |
| 内存文档 IO（`.tdoc` / `.trscn` / `.obj`） | [`src/engine/document/document_io.h`](https://github.com/terry-chao/tamias/blob/main/src/engine/document/document_io.h) | 客户端上传字节即可，不需要服务端碰文件系统 |
| .NET 8 工具链 | 根目录 [`Directory.Build.props`](https://github.com/terry-chao/tamias/blob/main/Directory.Build.props) | `net8.0` 已经是全仓默认，服务壳不用引新工具链 |
| CMake 里的 .NET 集成 | [`cmake/TamiasDotnet.cmake`](https://github.com/terry-chao/tamias/blob/main/cmake/TamiasDotnet.cmake) | `tamias_publish_server` 与 `tamias_publish_csharp` 并列 |
| 事务（一次求值 = 一条撤销） | [`src/command/core/command_system.h`](https://github.com/terry-chao/tamias/blob/main/src/command/core/command_system.h) | 服务端的「作业」直接映射成一条撤销记录 |

### 2.1 与插件那条边界的**方向**正好相反

这是最容易看糊的一点，值得单独写：

| | 谁装谁 | 边界 | 谁调用谁 |
|---|---|---|---|
| 插件（既有） | **C++ 宿主装 .NET**（`hostfxr` / `nethost`） | `HostApi` 结构体（函数指针表） | C++ 调托管 |
| 服务端（本决策） | **.NET 宿主装 C++**（P/Invoke） | `extern "C"` 扁平函数集 | 托管调 C++ |

[`src/plugin/csharp_runtime.cpp`](https://github.com/terry-chao/tamias/blob/main/src/plugin/csharp_runtime.cpp)
里已经有一套成熟的 hostfxr 代码，但**服务端不需要它**：服务壳自己就是 .NET 进程，
原生侧再加载一遍 CoreCLR 是反的。所以 `tamias_server` 刻意**不链** `tamias::plugin`
（也就不链 nethost）——依赖表见 [`src/server/CMakeLists.txt`](https://github.com/terry-chao/tamias/blob/main/src/server/CMakeLists.txt)。

## 3. 架构

```
客户端（浏览器 / 桌面 / 第三方）                       服务端
        │  HTTP / JSON + 原始字节                              │
        ▼                                                      ▼
  ┌────────────────────────────┐              ┌───────────────────────────────┐
  │  Web UI（web/，渲染留在本地）│              │  Tamias.Server（.NET 8 壳）    │
  └────────────────────────────┘              │   会话表 / 作业边界 / 错误码映射 │
                                              │        │ P/Invoke（扁平 C ABI）│
                                              │        ▼                       │
                                              │  tamias_server.dll             │
                                              │   SessionService               │
                                              │        │                       │
                                              │        ▼                       │
                                              │  Session ──► Dispatch / 事务    │
                                              │        └──► Document / 内核     │
                                              │             （OCCT / Truck）    │
                                              └───────────────────────────────┘
```

### 3.1 三种负载，三种归属

**判据只有一条：这个操作是「连续型」还是「提交型」。**

| 负载 | 例子 | 归属 | 为什么 |
|---|---|---|---|
| 连续型（每帧 / 每次鼠标） | 渲染、相机、拾取、显隐、剖切、测量 | **客户端** | 预算 <16 ms，网络一进回路"跟手感"就没了 |
| 提交型（命令提交后等结果） | BRep 布尔 / 倒角 / 抽壳、特征树重求值、大 IFC 解析、批量出图 | **服务端** | 可容忍秒级；服务端不受 wasm 的限制 |
| 首屏 / 大模型加载 | 全量 BIM、超大装配 | **服务端**（切片下发）或**按需 wasm 模块** | 见 [§3.2](#32-服务端相对-wasm-的真实收益) |

### 3.2 服务端相对 wasm 的真实收益

不是"更快"，是**换掉了 wasm 的三条硬限制**（数字都是实测的）：

| 限制 | 证据 | 服务端 |
|---|---|---|
| 内核缺失：wasm 里 `TAMIAS_ENABLE_OCCT=OFF` | [CMakePresets.json](https://github.com/terry-chao/tamias/blob/main/CMakePresets.json) 的 wasm 预设 | 同一份 C++ 源码原生编译，OCCT / IfcOpenShell 全可用 |
| 堆上限 2 GiB | wasm 产物里 `(memory $0 1024 32768)` = 起 64 MiB、上限 32768 页 | 无此限制 |
| 单线程 | `RenderDeviceConfig.synchronous = true`，避开 SharedArrayBuffer + COOP/COEP | 多核可用 |

代价同样要认（这一节不是单方面推销）：每人一份 CPU 成本、网络往返进入交互回路、
**数据主权**（BIM 客户常常不允许模型出内网——这一条能跟技术无关地否掉整个方案）、
部署复杂度反向转移、离线 / 内网能力丧失。

### 3.3 服务端化**不会**浪费 C++ 投入

因为 wasm 是**同一份源码的新目标平台**，不是新语言。`document / io / command / entity /
modeling` 全是 Qt-free、平台无关的：同一份源码既能 `emcc` 编成 wasm，也能原生编成服务端
二进制。所以这是「同一份内核换执行位置」，不是两条技术栈。

## 4. 契约：扁平 C ABI

头文件是唯一契约：[`src/server/server_api.h`](https://github.com/terry-chao/tamias/blob/main/src/server/server_api.h)。

三条硬约定（改之前先读头文件顶部的注释）：

1. **只追加**：不改既有函数签名、不改枚举既有值。加函数要抬 `TAMIAS_SERVER_ABI_VERSION`
   （当前 **1**），服务壳启动时用 `tamias_server_abi_version()` 握手，对不上就拒绝启动——
   宁可起不来，也不要按错位的表调用。
2. **边界粗粒度**：一次调用做完一整件事。几何（网格 / Brep / 内核对象）永远不过边界；
   过边界的只有命令名、参数文本和只读字符串。
3. **句柄不是线程安全的**：一个会话句柄同一时刻只能被一个线程使用。并行请求由服务壳
   按会话分派——托管侧唯一落实这条约定的是 [`NativeSession.cs`](https://github.com/terry-chao/tamias/blob/main/server/csharp/Tamias.Server/NativeSession.cs) 里那把锁。

### 4.1 为什么异常必须挡在边界内侧

引擎会抛（几何内核、`std::bad_alloc`、`std::filesystem`）。让 C++ 异常穿过 `extern "C"`
进到 .NET 是未定义行为——P/Invoke 那边看到的是**进程崩溃**，不是异常。
所以 `src/server/server_api.cpp` 里每个入口都是 `try/catch` → 错误码 + `last_error()`。

### 4.2 错误码 → HTTP 只有一处映射

[`HttpResults.cs`](https://github.com/terry-chao/tamias/blob/main/server/csharp/Tamias.Server/HttpResults.cs)：
句柄错 → 404，参数错 → 400，不支持的文档类型 → 415，其余（命令不存在 / 内核没注册 /
几何失败）→ **409 而不是 500**，因为重试同一个请求还会失败，客户端该改请求、不该重试。

## 5. 与既有 HostApi 的关系（不是第二套 API）

`HostApi`（[`src/plugin/host_api.h`](https://github.com/terry-chao/tamias/blob/main/src/plugin/host_api.h)）
是**函数指针表**，由 C++ 宿主填好交给托管侧；服务端反过来要的是**可被 P/Invoke 的导出
函数**。形状不同，所以没有直接复用那张表。但复用的是**语义与纪律**：

| | `HostApi`（插件） | 服务端 ABI |
|---|---|---|
| 写入口 | `dispatch` | `tamias_session_dispatch` |
| 参数格式 | 同一解析器 | 同一解析器 |
| 读 | `entity_*` / `feature_*` / 选择 | `entity_*` / 选择 |
| 事务 | `begin/commit/abort_transaction` | 同名一族 |
| 版本 | `kHostApiVersion` = 8 | `TAMIAS_SERVER_ABI_VERSION` = 1 |
| 字符串约定 | 写 UTF-8 + `'\0'`，返回写入长度 | **逐字相同**（`fill_utf8`，含截断语义） |

**为什么不直接复用 `PluginHost` 的 C++ 实现**：它把插件加载（`csharp_runtime.cpp` /
nethost）一起拉进来，而服务壳本身就是 .NET 进程（见 [§2.1](#21-与插件那条边界的**方向**正好相反)）。
代价是那几个 thunk 的语义在 `src/server/server_api.cpp` 里重写了一遍（`fill_utf8`、
实体排序）。如果将来这套 thunk 需要跟 `HostApi` 一起长，正确的做法是把它抽到 `src/host/`
共用一个提供者——那是一次**重构**，不该混在这个骨架里做。

## 6. 已知缺口

| 缺口 | 影响 | 计划 |
|---|---|---|
| 实体表是**每实体 1~3 次跨界调用** | 上百实体没问题，上万装配体会 CPU bound | 加 `tamias_session_entities_json` 批量口（一次拿完，JSON 或扁平结构）；HTTP 侧现在默认 `limit=200`、上限 2000 |
| 快照导出（`.trscn`）只有**读**没有写 | 服务端还不能把结果快照下发给客户端复用 | 加 `tamias_session_export_scene`（写进 `Result<std::vector<uint8_t>>`） |
| 服务端至今只有 Truck 的**桌面**内核可用 | `register_linked_kernels()` 在服务端已注册 OCCT（桌面预设 `TAMIAS_ENABLE_OCCT=ON`） | 已满足；wasm 侧才需要按需模块 |
| 没有认证 / 会话隔离 | 谁都能建会话、跑命令 | 传输层的事，属于下一步 |

## 7. 备选方案与为什么没选

| 选项 | 判断 |
|---|---|
| **纯 C++ 服务端**（Boost.Beast / Asio） | **可行**，且是"一个二进制、最少依赖"的最优解；但服务层的业务（账号、权限、协作、任务编排）用 C++ 写没有优势，而团队已有 .NET 8 工具链。作为**降级路径**保留：`tamias_server.dll` 的 C ABI 对任何 C++ 壳都同样可用。 |
| **Python** | 唯一诱惑是 IfcOpenShell 的 Python 绑定——但它就是那个 C++ 库的绑定。在 C++ 里直接用同一份库：没有 GIL、没有性能税、不多一份部署。**Python 适合离线批处理 / CI 模型检查**，不适合放进在线请求路径。 |
| **Go / Node / Java 做壳 + FFI** | 等于白加一层 cgo / N-API / JNI 边界，又没有 .NET 那样的先例密度（Rhino Compute 是 ASP.NET Core 托几何内核；Speckle 服务端是 .NET）。除非团队里这些语言压倒性熟练。 |
| **Rust** | 除非重写内核，否则只是又一层 FFI。 |

## 8. 未决事项

按重要性排，**这些还没做，别当承诺读**：

1. **会话存储**：现在会话在**进程内存**里（`SessionRegistry`），多副本部署要么做会话
   黏性路由，要么把会话搬进共享存储。**这是上生产前必须先回答的问题。**
2. **认证与租户隔离**：目前无认证。谁都可以建会话、跑任意命令。
3. **长任务模型**：OCCT 布尔可能是秒级。现在请求是同步阻塞的，需要作业队列 +
   轮询/推送（SSE 或 WebSocket）才能撑住并发。
4. **`HostApi` 与服务端 ABI 是否合并**：见 [§5](#5-与既有-hostapi-的关系不是第二套-api)。
5. **几何结果的传输格式**：现在客户端只能拿实体名/类别，拿不到网格。要么走
   `.trscn` 快照导出（[§6](#6-已知缺口)），要么让服务端只当"求值器"、网格仍由客户端
   自己 tessellate。**这个决定会影响客户端架构，值得单独一轮。**

## 9. 怎么构建、怎么跑

```powershell
# 1) 原生库（增量走现有 build/）
powershell -File scripts/build-server.ps1

# 2) .NET 服务壳
dotnet build server/csharp/Tamias.Server/Tamias.Server.csproj -c Release

# 3) 两者一起（CMake 把壳发布到原生库旁边）
powershell -File scripts/build-server.ps1 -WithShell
```

- `scripts/server-build.cmd` 是 ASCII 薄启动器（**必须保持 ASCII**——cmd 按控制台代码页
  读它，非 ASCII 注释会打乱解析；同理 `scripts/build-server.ps1` 也是 ASCII）。
- 干净目录用 CMakePresets.json 的 `server` 预设（`binaryDir = build/server`）。
- 开关：`TAMIAS_BUILD_SERVER`（原生库，缺省 **OFF**，不影响桌面 / wasm 两条产线）、
  `TAMIAS_BUILD_SERVER_SHELL`（顺便发布 .NET 壳）。
- 接口清单：`GET /` 会返回路由表；`GET /health` 返回原生版本与 ABI 号。

一份最小的端到端调用（会话 → 批作业 → 读回）：

```http
POST /v1/sessions                      {"name": "示例"}
POST /v1/sessions/{id}/batch           {"transactionName": "立柱网",
                                        "commands": [
                                          {"command": "create_column", "args": "s:sub_type=rect;d:width=0.4;d:depth=0.4;d:height=3.0;p:points=0,0,0"},
                                          {"command": "create_beam",   "args": "d:width=0.3;d:depth=0.4;p:points=-1.5,3,0|1.5,3,0"}
                                        ]}
GET  /v1/sessions/{id}/entities?limit=10
POST /v1/sessions/{id}/undo            → 一次退回**整个作业**
```

最后一步是这个设计的要点：客户端按一次撤销，退回的是**一个作业**，而不是作业中间的半步。
