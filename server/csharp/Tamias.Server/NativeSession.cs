using System.Text;

namespace Tamias.Server;

/// <summary>原生错误码 + 原文。HTTP 层按 <see cref="Code"/> 映射状态码。</summary>
internal sealed class ServerException(int code, string message) : Exception(message)
{
    public int Code { get; } = code;
}

/// <summary>
/// 一个原生会话（src/server 的 <c>SessionService</c>）。
///
/// <para><b>原生句柄不是线程安全的</b>（server_api.h 约定 3），所以这里用一把锁把同一个
/// 会话的原生调用串行化——这是那条约定在托管侧的唯一落点。会话之间仍然并行。</para>
///
/// <para><b>已知的粗粒度缺口</b>：<see cref="Entities"/> 是每实体 1~3 次跨界调用。
/// 几百个实体还行，上万的装配体需要一次拿完——计划中的
/// <c>tamias_session_entities_json</c> 批量口，见 docs/DECISION-SERVER-LANGUAGE.md §6。
/// 所以 HTTP 层对条数做了默认限流。</para>
/// </summary>
internal sealed class NativeSession : IDisposable
{
    // 512 字节够放文档名 / 实体名 / 类别名；不够会被原生侧截断而不是报错。
    private const int StringCap = 512;

    private readonly object gate_ = new();
    private IntPtr handle_;

    private NativeSession(IntPtr handle) => handle_ = handle;

    /// <summary>会话 id（托管侧生成，HTTP 路径里用它）。</summary>
    public Guid Id { get; } = Guid.NewGuid();

    public static NativeSession Create(string? documentName)
        => new(NativeServer.CreateSession(documentName));

    public void Dispose()
    {
        lock (gate_)
        {
            if (handle_ == IntPtr.Zero)
            {
                return;
            }
            NativeServer.DestroySession(handle_);
            handle_ = IntPtr.Zero;
        }
    }

    // ── 窄写 ─────────────────────────────────────────────────────────────────

    public void Dispatch(string command, string? args)
    {
        lock (gate_)
        {
            Check(NativeServer.Dispatch(handle_, command, args));
        }
    }

    public void BeginTransaction(string? name)
    {
        lock (gate_)
        {
            Check(NativeServer.BeginTransaction(handle_, name));
        }
    }

    public void CommitTransaction()
    {
        lock (gate_)
        {
            Check(NativeServer.CommitTransaction(handle_));
        }
    }

    /// <summary>回滚事务。返回被回滚的命令条数（0 = 空事务）。</summary>
    public void AbortTransaction()
    {
        lock (gate_)
        {
            Check(NativeServer.AbortTransaction(handle_));
        }
    }

    public void Undo()
    {
        lock (gate_)
        {
            Check(NativeServer.Undo(handle_));
        }
    }

    public void Redo()
    {
        lock (gate_)
        {
            Check(NativeServer.Redo(handle_));
        }
    }

    /// <summary>
    /// 一批命令 = 一条撤销记录，且**整批期间独占这个会话**。
    ///
    /// 独占是必须的：如果只让每次 Dispatch 各自加锁，另一个请求就能在 begin 与 commit
    /// 之间插进来，把它的编辑一并卷进这次事务——提交后变成一条谁也认不出的撤销记录。
    /// C# 的 lock 可重入，所以嵌着调本类那些同样加锁的方法是正确的。
    /// </summary>
    /// <returns>成功执行的命令条数。</returns>
    public int RunBatch(string? transactionName, IReadOnlyList<(string Command, string? Args)> commands)
    {
        lock (gate_)
        {
            BeginTransaction(transactionName);
            var executed = 0;
            try
            {
                foreach (var (command, args) in commands)
                {
                    Dispatch(command, args);
                    executed++;
                }
            }
            catch
            {
                // 一条失败就整批回滚：服务端一个作业要么全成、要么全不成。
                // 半途而废会让客户端的重试变成"重复编辑"。
                AbortTransaction();
                throw;
            }
            CommitTransaction();
            return executed;
        }
    }

    // ── 宽读 ─────────────────────────────────────────────────────────────────

    public string DocumentName()
    {
        lock (gate_)
        {
            var buffer = new byte[StringCap];
            var written = NativeServer.DocumentName(handle_, buffer, buffer.Length);
            if (written < 0)
            {
                throw new ServerException(NativeServer.Failed, $"document_name failed: {NativeServer.LastError}");
            }
            return Encoding.UTF8.GetString(buffer, 0, written);
        }
    }

    public bool CanUndo()
    {
        lock (gate_)
        {
            var rc = NativeServer.CanUndo(handle_);
            if (rc < 0)
            {
                throw new ServerException(NativeServer.Failed, $"can_undo failed: {NativeServer.LastError}");
            }
            return rc == 1;
        }
    }

    public bool CanRedo()
    {
        lock (gate_)
        {
            var rc = NativeServer.CanRedo(handle_);
            if (rc < 0)
            {
                throw new ServerException(NativeServer.Failed, $"can_redo failed: {NativeServer.LastError}");
            }
            return rc == 1;
        }
    }

    /// <summary>实体表。limit 之外的实体不取——见类型注释里的粗粒度缺口。</summary>
    public IReadOnlyList<EntitySummary> Entities(int limit)
    {
        lock (gate_)
        {
            var count = NativeServer.EntityCount(handle_);
            if (count < 0)
            {
                throw new ServerException(NativeServer.Failed, $"entity_count failed: {NativeServer.LastError}");
            }

            var take = Math.Min(count, limit);
            var entities = new List<EntitySummary>(take);
            for (var index = 0; index < take; index++)
            {
                Check(NativeServer.EntityIdAt(handle_, index, out var id));
                entities.Add(new EntitySummary(id, ReadString(id, NativeServer.EntityName),
                                               ReadString(id, NativeServer.EntityKind)));
            }
            return entities;
        }
    }

    public int EntityCount()
    {
        lock (gate_)
        {
            var count = NativeServer.EntityCount(handle_);
            if (count < 0)
            {
                throw new ServerException(NativeServer.Failed, $"entity_count failed: {NativeServer.LastError}");
            }
            return count;
        }
    }

    public IReadOnlyList<ulong> Selection()
    {
        lock (gate_)
        {
            var count = NativeServer.SelectionCount(handle_);
            if (count < 0)
            {
                throw new ServerException(NativeServer.Failed, $"selection_count failed: {NativeServer.LastError}");
            }

            var ids = new List<ulong>(count);
            for (var index = 0; index < count; index++)
            {
                Check(NativeServer.SelectionIdAt(handle_, index, out var id));
                ids.Add(id);
            }
            return ids;
        }
    }

    public void SetSelection(IReadOnlyList<ulong> ids)
    {
        lock (gate_)
        {
            // 空选择要显式传一个非空数组：原生侧对 count == 0 + nullptr 是合法的，
            // 但 ulong[0] 在封送时更省事，也避开"null 数组 = 没传"的歧义。
            var array = ids.Count > 0 ? ids.ToArray() : [];
            Check(NativeServer.SetSelection(handle_, array, array.Length));
        }
    }

    public void LoadBytes(string name, byte[] data)
    {
        lock (gate_)
        {
            Check(NativeServer.LoadBytes(handle_, name, data, data.Length));
        }
    }

    public SessionSummary Describe()
    {
        lock (gate_)
        {
            return new SessionSummary(
                Id,
                DocumentName(),
                EntityCount(),
                CanUndo(),
                CanRedo(),
                Selection());
        }
    }

    // ── 内部 ─────────────────────────────────────────────────────────────────

    private string ReadString(ulong id, Func<IntPtr, ulong, byte[], int, int> read)
    {
        var buffer = new byte[StringCap];
        var written = read(handle_, id, buffer, buffer.Length);
        return written <= 0 ? "" : Encoding.UTF8.GetString(buffer, 0, written);
    }

    private static void Check(int rc)
    {
        var error = NativeServer.Check(rc);
        if (error is not null)
        {
            throw error;
        }
    }
}
