using System.Runtime.InteropServices;

namespace Tamias.Api;

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate void HostLogFn(IntPtr context, int level, IntPtr utf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFillStringFn(IntPtr context, IntPtr utf8, int cap);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostCountFn(IntPtr context);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostIdAtFn(IntPtr context, int index, out ulong id);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostEntityStringFn(IntPtr context, ulong id, IntPtr utf8, int cap);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostDispatchFn(IntPtr context, IntPtr commandUtf8, IntPtr argsUtf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostRegisterCommandFn(
    IntPtr context,
    IntPtr idUtf8,
    IntPtr titleUtf8,
    IntPtr tooltipUtf8,
    IntPtr pageIdUtf8,
    IntPtr groupIdUtf8,
    IntPtr iconPathUtf8,
    int order,
    int flags);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostRegisterPluginFn(
    IntPtr context,
    IntPtr idUtf8,
    IntPtr nameUtf8,
    IntPtr authorUtf8,
    IntPtr versionUtf8,
    IntPtr releaseDateUtf8,
    IntPtr descriptionUtf8,
    IntPtr homepageUrlUtf8,
    IntPtr iconPathUtf8,
    int flags);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostBeginPointInputFn(
    IntPtr context,
    ulong requestId,
    int minPoints,
    int maxPoints,
    int flags,
    float workPlaneY,
    int previewKind,
    IntPtr previewCurveKindUtf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostCancelPointInputFn(IntPtr context, ulong requestId);

[StructLayout(LayoutKind.Sequential)]
public struct HostApi
{
    public int AbiVersion;
    public IntPtr Context;
    public IntPtr Log;
    public IntPtr DocumentName;
    public IntPtr EntityCount;
    public IntPtr EntityIdAt;
    public IntPtr EntityKind;
    public IntPtr EntityName;
    public IntPtr SelectionCount;
    public IntPtr SelectionIdAt;
    public IntPtr Dispatch;
    public IntPtr RegisterCommand;
    public IntPtr RegisterPlugin;
    public IntPtr BeginPointInput;
    public IntPtr CancelPointInput;
}
