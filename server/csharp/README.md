# Tamias 服务端（.NET 服务壳）

「C++ 内核 + C#/.NET 服务壳」的服务端半边。**决策、边界与未决事项看
[`docs/DECISION-SERVER-LANGUAGE.md`](../../docs/DECISION-SERVER-LANGUAGE.md)**，
这里只讲怎么跑。

分工一句话：**几何全在 C++（`src/server`，P/Invoke 进来的 `tamias_server`）**，
这个壳只做传输、会话表、错误码映射和作业（事务）边界。壳里没有一行几何代码。

## 怎么跑

```powershell
# 1) 原生库（增量走现有 build/；干净目录用 CMakePresets.json 的 server 预设）
powershell -File scripts/build-server.ps1

# 2) 发布本壳到原生库旁边（P/Invoke 按约定名 tamias_server 解析同目录）
dotnet publish server/csharp/Tamias.Server/Tamias.Server.csproj -c Release `
  -o build/bin/RelWithDebInfo

# 3) 起服务
build/bin/RelWithDebInfo/Tamias.Server.exe --urls http://127.0.0.1:5199
```

第 2 步也可以交给 CMake：`powershell -File scripts/build-server.ps1 -WithShell`
（`TAMIAS_BUILD_SERVER_SHELL=ON`，见 `cmake/TamiasDotnet.cmake` 的 `tamias_publish_server`）。

启动时会用 `tamias_server_abi_version()` 和 `NativeServer.ExpectedAbiVersion` 握手，
对不上直接拒绝启动——按错位的表调用比起不来危险得多。
`GET /` 返回路由表，`GET /health` 返回原生版本与 ABI 号。

## 接口

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/health` | 原生版本 + ABI 号（握手） |
| POST | `/v1/sessions` | `{name?}` → 会话快照（含 `id`） |
| GET | `/v1/sessions/{id}` | 会话快照 |
| DELETE | `/v1/sessions/{id}` | 销毁会话（释放原生句柄） |
| GET | `/v1/sessions/{id}/entities?limit=200` | 实体表（默认 200、上限 2000，原因见下） |
| POST | `/v1/sessions/{id}/commands` | `{command, args}` → 执行一条命令 |
| POST | `/v1/sessions/{id}/batch` | `{transactionName?, commands[]}` → **一批 = 一条撤销记录** |
| POST | `/v1/sessions/{id}/selection` | `{ids[]}` → 整体替换选择（`[]` 清空） |
| POST | `/v1/sessions/{id}/document?name=model.obj` | 原始字节进 body（`.tdoc` / `.trscn` / `.obj`） |
| POST | `/v1/sessions/{id}/undo` | 撤销；栈空时是无操作 |
| POST | `/v1/sessions/{id}/redo` | 重做 |

### 错误码映射只有一处

见 `HttpResults.cs`：句柄错 → 404，参数错 → 400，不支持的文档类型 → 415，
其余（命令不存在 / 内核没注册 / 几何失败）→ **409 而不是 500**——
重试同一个请求还会失败，客户端该改请求、不该重试。

## 一次完整的作业

```powershell
$base = "http://127.0.0.1:5199"
$id = (Invoke-RestMethod -Method Post "$base/v1/sessions" -ContentType application/json `
        -Body '{"name":"示例"}').id

$batch = @{ transactionName = "立柱网"; commands = @(
  @{ command = "create_column"; args = "s:sub_type=rect;d:width=0.4;d:depth=0.4;d:height=3.0;p:points=0,0,0" },
  @{ command = "create_beam";   args = "d:width=0.3;d:depth=0.4;p:points=0,3,0|3,3,0" }
) } | ConvertTo-Json -Depth 5
Invoke-RestMethod -Method Post "$base/v1/sessions/$id/batch" -ContentType application/json -Body $batch

# 一次退回**整个作业**，而不是作业中间的半步
Invoke-RestMethod -Method Post "$base/v1/sessions/$id/undo"
```

`args` 的格式与桌面命令控制台**逐字相同**（同一个解析器
`src/host/command_arg_text.h`）。

## 两条边界纪律（改代码前先读）

1. **原生句柄不是线程安全的**：一个会话句柄同一时刻只能被一个线程用。
   托管侧唯一落实这条的是 `NativeSession` 里那把锁；`RunBatch` 在**整批期间**持有它，
   否则另一个请求会插进 `begin` 与 `commit` 之间，把它的编辑一并卷进这次事务。
2. **边界粗粒度**：几何永远不过边界，过边界的只有命令名、参数文本和只读字符串。
   现在唯一的例外是实体表（每实体 1~3 次跨界调用），所以 HTTP 侧限流到 2000；
   批量口落地前别把 `limit` 放大。

## 已知缺口 / 未决事项

完整列表见 [`docs/DECISION-SERVER-LANGUAGE.md`](../../docs/DECISION-SERVER-LANGUAGE.md) §6 与 §8。
上生产前必须先回答的两条：

- **会话在进程内存里**（`SessionRegistry`）：多副本部署要么会话黏性路由，要么共享存储。
- **无认证**：谁都能建会话、跑任意命令。

另外长任务（OCCT 布尔可能是秒级）现在是同步阻塞的，需要作业队列 + 轮询/推送。
