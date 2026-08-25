using System.Runtime.InteropServices;
using Tamias.Api;

namespace Tamias.Host;

sealed class Host : IHost
{
    readonly HostApi api_;
    readonly Dictionary<string, Action> actions_ = new(StringComparer.Ordinal);
    readonly Dictionary<ulong, Action<PointInputResult>> pointInputCallbacks_ = [];
    readonly object pointInputLock_ = new();
    ulong nextPointInputRequestId_;

    public Host(HostApi api)
    {
        api_ = api;
    }

    public IReadOnlyDictionary<string, Action> Actions => actions_;

    public void RegisterPlugin(PluginMetadata metadata)
    {
        ArgumentNullException.ThrowIfNull(metadata);
        ArgumentException.ThrowIfNullOrEmpty(metadata.Id);
        var fn = As<HostRegisterPluginFn>(api_.RegisterPlugin);
        var idPtr = Utf8(metadata.Id);
        var namePtr = Utf8(metadata.Name);
        var authorPtr = Utf8(metadata.Author);
        var versionPtr = Utf8(metadata.Version);
        var releaseDatePtr = Utf8(metadata.ReleaseDate);
        var descriptionPtr = Utf8(metadata.Description);
        var homepageUrlPtr = Utf8(metadata.HomepageUrl);
        var iconPathPtr = Utf8(metadata.IconPath);
        try
        {
            var flags = metadata.IsBuiltIn ? 1 : 0;
            if (fn(
                    api_.Context,
                    idPtr,
                    namePtr,
                    authorPtr,
                    versionPtr,
                    releaseDatePtr,
                    descriptionPtr,
                    homepageUrlPtr,
                    iconPathPtr,
                    flags) != 0)
            {
                throw new InvalidOperationException("Failed to register plugin '" + metadata.Id + "'");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(idPtr);
            Marshal.FreeCoTaskMem(namePtr);
            Marshal.FreeCoTaskMem(authorPtr);
            Marshal.FreeCoTaskMem(versionPtr);
            Marshal.FreeCoTaskMem(releaseDatePtr);
            Marshal.FreeCoTaskMem(descriptionPtr);
            Marshal.FreeCoTaskMem(homepageUrlPtr);
            Marshal.FreeCoTaskMem(iconPathPtr);
        }
    }

    public string DocumentName => ReadString(As<HostFillStringFn>(api_.DocumentName));

    public IReadOnlyList<EntityInfo> Entities
    {
        get
        {
            var countFn = As<HostCountFn>(api_.EntityCount);
            var idAt = As<HostIdAtFn>(api_.EntityIdAt);
            var kindFn = As<HostEntityStringFn>(api_.EntityKind);
            var nameFn = As<HostEntityStringFn>(api_.EntityName);
            var n = countFn(api_.Context);
            var list = new List<EntityInfo>(Math.Max(n, 0));
            for (var i = 0; i < n; ++i)
            {
                if (idAt(api_.Context, i, out var id) != 0)
                {
                    continue;
                }
                list.Add(new EntityInfo(id, ParseKind(ReadEntityString(kindFn, id)), ReadEntityString(nameFn, id)));
            }
            return list;
        }
    }

    public IReadOnlyList<ulong> Selection
    {
        get
        {
            var countFn = As<HostCountFn>(api_.SelectionCount);
            var idAt = As<HostIdAtFn>(api_.SelectionIdAt);
            var n = countFn(api_.Context);
            var list = new List<ulong>(Math.Max(n, 0));
            for (var i = 0; i < n; ++i)
            {
                if (idAt(api_.Context, i, out var id) == 0)
                {
                    list.Add(id);
                }
            }
            return list;
        }
    }

    public void Log(string message)
    {
        var fn = As<HostLogFn>(api_.Log);
        var p = Utf8(message);
        try
        {
            fn(api_.Context, 0, p);
        }
        finally
        {
            Marshal.FreeCoTaskMem(p);
        }
    }

    public void Dispatch(string command, CommandArgs? args = null)
    {
        var fn = As<HostDispatchFn>(api_.Dispatch);
        var c = Utf8(command);
        var a = Utf8(args?.ToString() ?? "");
        try
        {
            if (fn(api_.Context, c, a) != 0)
            {
                throw new InvalidOperationException("Host dispatch failed for '" + command + "'");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(c);
            Marshal.FreeCoTaskMem(a);
        }
    }

    public void AddCommand(
        string id,
        string title,
        Action action,
        string? tooltip = null,
        RibbonPlacement? placement = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        ArgumentNullException.ThrowIfNull(action);
        placement ??= new RibbonPlacement();
        var fn = As<HostRegisterCommandFn>(api_.RegisterCommand);
        var idPtr = Utf8(id);
        var titlePtr = Utf8(title);
        var tipPtr = Utf8(tooltip ?? "");
        var pagePtr = Utf8(placement.PageId);
        var groupPtr = Utf8(placement.GroupId);
        var iconPtr = Utf8(placement.IconPath ?? "");
        try
        {
            var flags = placement.Checkable ? 1 : 0;
            if (fn(
                    api_.Context,
                    idPtr,
                    titlePtr,
                    tipPtr,
                    pagePtr,
                    groupPtr,
                    iconPtr,
                    placement.Order,
                    flags) != 0)
            {
                throw new InvalidOperationException("Failed to register command '" + id + "'");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(idPtr);
            Marshal.FreeCoTaskMem(titlePtr);
            Marshal.FreeCoTaskMem(tipPtr);
            Marshal.FreeCoTaskMem(pagePtr);
            Marshal.FreeCoTaskMem(groupPtr);
            Marshal.FreeCoTaskMem(iconPtr);
        }
        actions_[id] = action;
    }

    public ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> callback)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(callback);

        ulong requestId;
        lock (pointInputLock_)
        {
            do
            {
                requestId = ++nextPointInputRequestId_;
            }
            while (requestId == 0);
            pointInputCallbacks_.Add(requestId, callback);
        }

        var curveKindPtr = Utf8(options.PreviewCurveKind ?? "");
        try
        {
            var fn = As<HostBeginPointInputFn>(api_.BeginPointInput);
            if (fn(
                    api_.Context,
                    requestId,
                    options.MinPoints,
                    options.MaxPoints,
                    options.Flags,
                    options.WorkPlaneY,
                    (int)options.PreviewKind,
                    curveKindPtr) != 0)
            {
                lock (pointInputLock_)
                {
                    pointInputCallbacks_.Remove(requestId);
                }
                throw new InvalidOperationException("Host failed to begin point input");
            }
        }
        catch
        {
            lock (pointInputLock_)
            {
                pointInputCallbacks_.Remove(requestId);
            }
            throw;
        }
        finally
        {
            Marshal.FreeCoTaskMem(curveKindPtr);
        }
        return requestId;
    }

    public void CancelPointInput(ulong requestId)
    {
        var fn = As<HostCancelPointInputFn>(api_.CancelPointInput);
        if (fn(api_.Context, requestId) != 0)
        {
            throw new InvalidOperationException("Host failed to cancel point input");
        }
    }

    internal void CompletePointInput(ulong requestId, IReadOnlyList<PickPoint> points, bool cancelled)
    {
        Action<PointInputResult>? callback;
        lock (pointInputLock_)
        {
            if (!pointInputCallbacks_.Remove(requestId, out callback))
            {
                return;
            }
        }
        callback(new PointInputResult(points, cancelled));
    }

    static T As<T>(IntPtr fn) where T : Delegate
    {
        return Marshal.GetDelegateForFunctionPointer<T>(fn);
    }

    static IntPtr Utf8(string text)
    {
        return Marshal.StringToCoTaskMemUTF8(text);
    }

    string ReadString(HostFillStringFn fn)
    {
        const int cap = 512;
        var buf = Marshal.AllocCoTaskMem(cap);
        try
        {
            fn(api_.Context, buf, cap);
            return Marshal.PtrToStringUTF8(buf) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(buf);
        }
    }

    string ReadEntityString(HostEntityStringFn fn, ulong id)
    {
        const int cap = 256;
        var buf = Marshal.AllocCoTaskMem(cap);
        try
        {
            if (fn(api_.Context, id, buf, cap) < 0)
            {
                return "";
            }
            return Marshal.PtrToStringUTF8(buf) ?? "";
        }
        finally
        {
            Marshal.FreeCoTaskMem(buf);
        }
    }

    static EntityKind ParseKind(string name)
    {
        return Enum.TryParse<EntityKind>(name, ignoreCase: true, out var kind) ? kind : EntityKind.Unknown;
    }
}
