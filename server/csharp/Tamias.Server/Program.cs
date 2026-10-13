using Tamias.Server;

// Tamias 服务端：.NET 服务壳 + C++ 几何内核。
//
// 分工（见 docs/DECISION-SERVER-LANGUAGE.md §3）：
//   - C++（src/server，P/Invoke 进来的 tamias_server）拥有**几何**：文档、命令、
//     内核求值、撤销栈。服务壳里没有一行几何代码。
//   - 这个壳拥有**传输与编排**：HTTP、会话表、错误码映射、作业（事务）边界。
//
// 契约面刻意很窄：写只有 dispatch 一条路（与桌面 / 插件 / AI 同一条命令注册表），
// 读是只读快照。几何对象永远不过边界。

const int DefaultEntityLimit = 200;

// 原生侧目前是每实体 1~3 次跨界调用（见 NativeSession 的「粗粒度缺口」注释），
// 所以这里必须有上限。等 tamias_session_entities_json 批量口落地后可以放宽。
const int MaxEntityLimit = 2000;

var builder = WebApplication.CreateBuilder(args);

// 会话表是进程级的。容器停机时会 dispose 它，每个原生会话随之释放。
builder.Services.AddSingleton<SessionRegistry>();

var app = builder.Build();

// 启动握手：ABI 对不上就别起。按错位的表调用比起不来危险得多。
var abi = NativeServer.AbiVersion;
if (abi != NativeServer.ExpectedAbiVersion)
{
    app.Logger.LogCritical(
        "tamias_server abi {Actual} != expected {Expected}; refusing to start",
        abi,
        NativeServer.ExpectedAbiVersion);
    return 1;
}

// 幂等的进程级初始化（注册建模内核 + 命令注册表）。建会话时原生侧也会自动调，
// 这里显式调一次，好把失败报在启动而不是第一个请求上。
NativeServer.Init();
app.Logger.LogInformation("tamias_server {Version} ready (abi {Abi})", NativeServer.Version, abi);

app.MapGet("/", () => Results.Ok(new
{
    service = "tamias-server",
    native = NativeServer.Version,
    abi,
    routes = new[]
    {
        "GET    /health",
        "POST   /v1/sessions",
        "GET    /v1/sessions/{id}",
        "DELETE /v1/sessions/{id}",
        "GET    /v1/sessions/{id}/entities?limit=200",
        "POST   /v1/sessions/{id}/commands      { command, args }",
        "POST   /v1/sessions/{id}/batch         { transactionName, commands[] }",
        "POST   /v1/sessions/{id}/selection     { ids[] }",
        "POST   /v1/sessions/{id}/document?name=model.obj   (raw bytes)",
        "POST   /v1/sessions/{id}/undo",
        "POST   /v1/sessions/{id}/redo",
    },
}));

app.MapGet("/health", () => Results.Ok(new HealthResponse(NativeServer.Version, abi)));

// ── 会话生命周期 ─────────────────────────────────────────────────────────────

app.MapPost("/v1/sessions", (CreateSessionRequest? request, SessionRegistry registry) =>
    HttpResults.Run(() =>
    {
        var session = registry.Create(request?.Name);
        return Results.Created($"/v1/sessions/{session.Id}", session.Describe());
    }));

app.MapGet("/v1/sessions/{id:guid}", (Guid id, SessionRegistry registry) =>
    registry.TryGet(id, out var session)
        ? HttpResults.Run(() => Results.Ok(session.Describe()))
        : HttpResults.Missing(id));

app.MapDelete("/v1/sessions/{id:guid}", (Guid id, SessionRegistry registry) =>
    registry.Remove(id) ? Results.NoContent() : HttpResults.Missing(id));

// ── 宽读 ─────────────────────────────────────────────────────────────────────

app.MapGet("/v1/sessions/{id:guid}/entities", (Guid id, int? limit, SessionRegistry registry) =>
{
    if (!registry.TryGet(id, out var session))
    {
        return HttpResults.Missing(id);
    }
    var take = Math.Clamp(limit ?? DefaultEntityLimit, 1, MaxEntityLimit);
    return HttpResults.Run(() => Results.Ok(session.Entities(take)));
});

// ── 窄写：只有这条路 ─────────────────────────────────────────────────────────

app.MapPost("/v1/sessions/{id:guid}/commands",
    (Guid id, CommandRequest request, SessionRegistry registry) =>
    {
        if (!registry.TryGet(id, out var session))
        {
            return HttpResults.Missing(id);
        }
        if (string.IsNullOrWhiteSpace(request.Command))
        {
            return HttpResults.Misuse("command must not be empty");
        }
        return HttpResults.Run(() =>
        {
            session.Dispatch(request.Command, request.Args);
            return Results.Ok(session.Describe());
        });
    });

// 一个作业 = 一批命令 = **一条**撤销记录。这是服务端相对"逐条命令"的主要价值：
// 客户端按一次 Ctrl+Z 能整体退回，而不是退回中间的半步。
app.MapPost("/v1/sessions/{id:guid}/batch", (Guid id, BatchRequest request, SessionRegistry registry) =>
{
    if (!registry.TryGet(id, out var session))
    {
        return HttpResults.Missing(id);
    }
    if (request.Commands is null || request.Commands.Count == 0)
    {
        return HttpResults.Misuse("commands must not be empty");
    }
    if (request.Commands.Exists(command => string.IsNullOrWhiteSpace(command.Command)))
    {
        return HttpResults.Misuse("every command needs a non-empty command id");
    }
    return HttpResults.Run(() =>
    {
        var planned = request.Commands.ConvertAll(command => (command.Command, command.Args));
        var executed = session.RunBatch(request.TransactionName, planned);
        return Results.Ok(new BatchResponse(executed, true, session.Describe()));
    });
});

app.MapPost("/v1/sessions/{id:guid}/selection",
    (Guid id, SelectionRequest request, SessionRegistry registry) =>
    {
        if (!registry.TryGet(id, out var session))
        {
            return HttpResults.Missing(id);
        }
        if (request.Ids is null)
        {
            return HttpResults.Misuse("ids is required (send [] to clear the selection)");
        }
        return HttpResults.Run(() =>
        {
            session.SetSelection(request.Ids);
            return Results.Ok(session.Describe());
        });
    });

// 栈空时是无操作并返回成功（幂等，便于 HTTP 侧重试）；有没有得撤看 Describe().CanUndo。
app.MapPost("/v1/sessions/{id:guid}/undo", (Guid id, SessionRegistry registry) =>
{
    if (!registry.TryGet(id, out var session))
    {
        return HttpResults.Missing(id);
    }
    return HttpResults.Run(() =>
    {
        session.Undo();
        return Results.Ok(session.Describe());
    });
});

app.MapPost("/v1/sessions/{id:guid}/redo", (Guid id, SessionRegistry registry) =>
{
    if (!registry.TryGet(id, out var session))
    {
        return HttpResults.Missing(id);
    }
    return HttpResults.Run(() =>
    {
        session.Redo();
        return Results.Ok(session.Describe());
    });
});

// ── 文档 IO ──────────────────────────────────────────────────────────────────
// 原始字节直接进 body（application/octet-stream），扩展名走 query。
// 不做 multipart：原生侧本来就是"内存文档 IO"（.tdoc / .trscn / .obj），
// 一次读进内存语义上没有损失，也少一层解析。
app.MapPost("/v1/sessions/{id:guid}/document",
    async (Guid id, HttpRequest request, SessionRegistry registry, CancellationToken ct) =>
    {
        if (!registry.TryGet(id, out var session))
        {
            return HttpResults.Missing(id);
        }
        var name = request.Query["name"].ToString();
        if (string.IsNullOrWhiteSpace(name))
        {
            return HttpResults.Misuse("query parameter 'name' is required, e.g. ?name=model.obj");
        }

        using var buffer = new MemoryStream();
        await request.Body.CopyToAsync(buffer, ct);
        var bytes = buffer.ToArray();
        return HttpResults.Run(() =>
        {
            session.LoadBytes(name, bytes);
            return Results.Ok(session.Describe());
        });
    });

app.Run();
return 0;
