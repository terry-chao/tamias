using System.Collections.Concurrent;

namespace Tamias.Server;

/// <summary>
/// 进程内的会话表。一个会话 = 一个原生 <c>SessionService</c> = 一个打开的文档。
///
/// <para><b>单实例假设</b>：表在进程内存里，所以多副本部署要么做会话黏性路由，要么把
/// 会话搬进共享存储。这是本项目前明确的未决事项，见
/// docs/DECISION-SERVER-LANGUAGE.md §7。</para>
///
/// <para>由 DI 容器作为单例持有，容器在停机时会调 <see cref="Dispose"/>，
/// 所以每个原生会话都会被释放——不会把文档留在原生堆上。</para>
/// </summary>
internal sealed class SessionRegistry : IDisposable
{
    private readonly ConcurrentDictionary<Guid, NativeSession> sessions_ = new();

    public int Count => sessions_.Count;

    public NativeSession Create(string? name)
    {
        var session = NativeSession.Create(name);
        sessions_[session.Id] = session;
        return session;
    }

    public bool TryGet(Guid id, out NativeSession session) => sessions_.TryGetValue(id, out session!);

    public bool Remove(Guid id)
    {
        if (!sessions_.TryRemove(id, out var session))
        {
            return false;
        }
        session.Dispose();
        return true;
    }

    public void Dispose()
    {
        foreach (var id in sessions_.Keys.ToArray())
        {
            if (sessions_.TryRemove(id, out var session))
            {
                session.Dispose();
            }
        }
    }
}
