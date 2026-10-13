namespace Tamias.Server;

// HTTP 面的契约。刻意保持"粗粒度作业"的形状：一次请求做完一整件事，
// 而不是把引擎的每个 getter 都暴露成一个端点。
//
// 反序列化目标一律用具体集合类型（List<T>）：IReadOnlyList<T> 之类留给**响应**。
// 请求体是外部输入，绑不上就是 400，不该赌序列化器的口味。

/// <summary>健康检查 / 握手。</summary>
public sealed record HealthResponse(string NativeVersion, int AbiVersion);

/// <summary>新建会话。Name 可以为空（原生侧退化成 "Untitled"）。</summary>
public sealed record CreateSessionRequest(string? Name);

/// <summary>
/// 会话状态快照。**带 id**：创建会话时 id 只出现在 Location 头里，
/// 客户端要能从响应体里直接拿到，不用去解析 HTTP 头。
/// </summary>
public sealed record SessionSummary(
    Guid Id,
    string Document,
    int EntityCount,
    bool CanUndo,
    bool CanRedo,
    IReadOnlyList<ulong> Selection);

/// <summary>单个实体的只读摘要。</summary>
public sealed record EntitySummary(ulong Id, string Name, string Kind);

/// <summary>一条命令：命令 id + 与桌面控制台同格式的参数文本。</summary>
public sealed record CommandRequest(string Command, string? Args);

/// <summary>
/// 一批命令 = 一条撤销记录，且整批期间独占该会话。
/// 这是服务端真正想要的形状：客户端提交一个作业，服务端把它当**一次编辑**执行完。
/// </summary>
public sealed record BatchRequest(string? TransactionName, List<CommandRequest>? Commands);

/// <summary>批作业结果。Committed=false 只会在将来支持"失败也提交"时出现。</summary>
public sealed record BatchResponse(int Executed, bool Committed, SessionSummary Session);

/// <summary>选择集（整体替换，不是增量）。空数组 = 清空选择。</summary>
public sealed record SelectionRequest(List<ulong>? Ids);

/// <summary>统一的错误体。</summary>
public sealed record ErrorResponse(string Error, int Code);
