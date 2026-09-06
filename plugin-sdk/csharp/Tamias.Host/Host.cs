using System.Runtime.InteropServices;
using Tamias.Api;

namespace Tamias.Host;

sealed class Host : IHost, IUi
{
    readonly HostApi api_;
    readonly Dictionary<string, Action> actions_ = new(StringComparer.Ordinal);
    readonly Dictionary<ulong, Action<PointInputResult>> pointInputCallbacks_ = [];
    readonly object pointInputLock_ = new();
    ulong nextPointInputRequestId_;
    bool alive_ = true;

    public Host(HostApi api)
    {
        api_ = api;
    }

    public IReadOnlyDictionary<string, Action> Actions => actions_;

    internal void Detach()
    {
        alive_ = false;
    }

    public void RegisterPlugin(PluginMetadata metadata)
    {
        if (!alive_)
        {
            return;
        }
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

    public string DocumentName =>
        alive_ ? ReadString(As<HostFillStringFn>(api_.DocumentName)) : "";

    public IReadOnlyList<EntityInfo> Entities
    {
        get
        {
            if (!alive_)
            {
                return [];
            }
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
            if (!alive_)
            {
                return [];
            }
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

    public IUi Ui => this;

    public void Log(string message)
    {
        if (!alive_)
        {
            return;
        }
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
        if (!alive_)
        {
            return;
        }
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
        if (!alive_)
        {
            return;
        }
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

    public void SetSelection(IEnumerable<ulong> ids)
    {
        ArgumentNullException.ThrowIfNull(ids);
        if (!alive_)
        {
            return;
        }
        var list = ids as ulong[] ?? ids.ToArray();
        var fn = As<HostSetSelectionFn>(api_.SetSelection);
        if (list.Length == 0)
        {
            if (fn(api_.Context, IntPtr.Zero, 0) != 0)
            {
                throw new InvalidOperationException("Host failed to clear selection");
            }
            return;
        }
        var ptr = Marshal.AllocCoTaskMem(checked(sizeof(ulong) * list.Length));
        try
        {
            for (var i = 0; i < list.Length; ++i)
            {
                Marshal.WriteInt64(ptr, i * sizeof(long), unchecked((long)list[i]));
            }
            if (fn(api_.Context, ptr, list.Length) != 0)
            {
                throw new InvalidOperationException("Host failed to set selection");
            }
        }
        finally
        {
            Marshal.FreeCoTaskMem(ptr);
        }
    }

    public void ClearSelection() => SetSelection([]);

    public DialogResult ShowMessage(string title, string message, DialogButtons buttons = DialogButtons.Ok)
    {
        var status = ShowDialog((int)PluginDialogKind.Message, (int)buttons, Spec(("T", title), ("V", message)), out _);
        return status < 0 ? DialogResult.None : (DialogResult)status;
    }

    public string? PromptString(string title, string label, string defaultValue = "")
    {
        var status = ShowDialog(
            (int)PluginDialogKind.PromptString,
            0,
            Spec(("T", title), ("L", label), ("V", defaultValue)),
            out var value);
        return status == 0 ? value : null;
    }

    public double? PromptNumber(string title, string label, double defaultValue = 0, double? min = null, double? max = null)
    {
        var parts = new List<(string Tag, string Value)>
        {
            ("T", title),
            ("L", label),
            ("V", defaultValue.ToString("G17", System.Globalization.CultureInfo.InvariantCulture)),
        };
        if (min.HasValue)
        {
            parts.Add(("MIN", min.Value.ToString("G17", System.Globalization.CultureInfo.InvariantCulture)));
        }
        if (max.HasValue)
        {
            parts.Add(("MAX", max.Value.ToString("G17", System.Globalization.CultureInfo.InvariantCulture)));
        }
        var status = ShowDialog((int)PluginDialogKind.PromptNumber, 0, Spec(parts.ToArray()), out var value);
        if (status != 0)
        {
            return null;
        }
        return double.Parse(value, System.Globalization.CultureInfo.InvariantCulture);
    }

    public bool ShowForm(PromptForm form)
    {
        ArgumentNullException.ThrowIfNull(form);
        var status = ShowDialog((int)PluginDialogKind.Form, 0, form.ToSpec(), out var value);
        if (status != 0)
        {
            return false;
        }
        form.ApplyResult(value);
        return true;
    }

    public string? OpenFile(string title, string filter)
    {
        var status = ShowDialog((int)PluginDialogKind.OpenFile, 0, Spec(("T", title), ("FILTER", filter)), out var value);
        return status == 0 ? value : null;
    }

    public string? SaveFile(string title, string filter, string? defaultName = null)
    {
        var status = ShowDialog(
            (int)PluginDialogKind.SaveFile,
            0,
            Spec(("T", title), ("FILTER", filter), ("NAME", defaultName ?? "")),
            out var value);
        return status == 0 ? value : null;
    }

    public ulong BeginPointInput(PointInputOptions options, Action<PointInputResult> callback)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(callback);
        if (!alive_)
        {
            throw new InvalidOperationException("Host is shutting down");
        }

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
        var filterKindPtr = Utf8(FilterName(options.FilterKind));
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
                    curveKindPtr,
                    filterKindPtr) != 0)
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
            Marshal.FreeCoTaskMem(filterKindPtr);
        }
        return requestId;
    }

    public ulong BeginEntityInput(EntityInputOptions options, Action<EntityInputResult> callback)
    {
        ArgumentNullException.ThrowIfNull(options);
        ArgumentNullException.ThrowIfNull(callback);
        return BeginPointInput(
            new PointInputOptions
            {
                MinPoints = options.MinCount,
                MaxPoints = options.MaxCount,
                AllowConfirm = options.AllowConfirm || options.MaxCount == 0,
                EntitiesOnly = true,
                FilterKind = options.FilterKind,
            },
            result => callback(new EntityInputResult(result.Points, result.Cancelled)));
    }

    public void CancelPointInput(ulong requestId)
    {
        if (!alive_)
        {
            return;
        }
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

    int ShowDialog(int kind, int buttons, string spec, out string value)
    {
        value = "";
        if (!alive_)
        {
            return -1;
        }
        var fn = As<HostShowDialogFn>(api_.ShowDialog);
        var specPtr = Utf8(spec);
        const int cap = 4096;
        var outPtr = Marshal.AllocCoTaskMem(cap);
        try
        {
            var status = fn(api_.Context, kind, buttons, specPtr, outPtr, cap);
            if (status == 0)
            {
                value = Marshal.PtrToStringUTF8(outPtr) ?? "";
            }
            return status;
        }
        finally
        {
            Marshal.FreeCoTaskMem(specPtr);
            Marshal.FreeCoTaskMem(outPtr);
        }
    }

    static string Spec(params (string Tag, string Value)[] parts)
    {
        return string.Join('\n', parts.Select(part =>
            part.Tag + "|" + (part.Value ?? "").Replace('|', ' ').Replace('\n', '\u001e')));
    }

    static string FilterName(EntityKind? kind)
    {
        return kind is null or EntityKind.Unknown ? "" : kind.Value.ToString();
    }

    enum PluginDialogKind
    {
        Message = 0,
        PromptString = 1,
        PromptNumber = 2,
        OpenFile = 3,
        SaveFile = 4,
        Form = 5,
    }
}
