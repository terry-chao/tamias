using System.Runtime.InteropServices;
using Tamias.Api;

namespace Tamias.Host;

sealed class Host : IHost
{
    readonly HostApi api_;
    readonly Dictionary<string, Action> actions_ = new(StringComparer.Ordinal);

    public Host(HostApi api)
    {
        api_ = api;
    }

    public IReadOnlyDictionary<string, Action> Actions => actions_;

    public void RegisterPlugin(string id, string title)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        var fn = As<HostRegisterPluginFn>(api_.RegisterPlugin);
        var idPtr = Utf8(id);
        var titlePtr = Utf8(title ?? id);
        try
        {
            if (fn(api_.Context, idPtr, titlePtr) != 0)
            {
                throw new InvalidOperationException("Failed to register plugin '" + id + "'");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(idPtr);
            Marshal.FreeCoTaskMem(titlePtr);
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

    public void AddCommand(string id, string title, Action action, string? tooltip = null)
    {
        ArgumentException.ThrowIfNullOrEmpty(id);
        ArgumentNullException.ThrowIfNull(action);
        var fn = As<HostRegisterCommandFn>(api_.RegisterCommand);
        var idPtr = Utf8(id);
        var titlePtr = Utf8(title);
        var tipPtr = Utf8(tooltip ?? "");
        try
        {
            if (fn(api_.Context, idPtr, titlePtr, tipPtr) != 0)
            {
                throw new InvalidOperationException("Failed to register command '" + id + "'");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(idPtr);
            Marshal.FreeCoTaskMem(titlePtr);
            Marshal.FreeCoTaskMem(tipPtr);
        }
        actions_[id] = action;
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
