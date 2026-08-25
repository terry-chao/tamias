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
public delegate int HostRegisterCommandFn(IntPtr context, IntPtr idUtf8, IntPtr titleUtf8, IntPtr tooltipUtf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostRegisterPluginFn(IntPtr context, IntPtr idUtf8, IntPtr titleUtf8);

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
}
