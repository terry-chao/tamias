using System.Runtime.InteropServices;

namespace Tamias.Api;

// C ABI 版本：必须与 C++ src/plugin/host_api.h 的 kHostApiVersion 一致。
// Bootstrap.Initialize 对不上就拒绝加载（宁可没有插件，也不要按错位的表调用）。
public static class HostApiVersion
{
    public const int Current = 8;
}

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
    IntPtr previewCurveKindUtf8,
    IntPtr filterKindUtf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostCancelPointInputFn(IntPtr context, ulong requestId);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostSetSelectionFn(IntPtr context, IntPtr ids, int count);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostShowDialogFn(
    IntPtr context,
    int kind,
    int buttons,
    IntPtr specUtf8,
    IntPtr outUtf8,
    int cap);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFeatureCountFn(IntPtr context, ulong entityId);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFeatureAtFn(
    IntPtr context,
    ulong entityId,
    int index,
    out ulong id,
    out int kind,
    out int inputCount,
    out int paramCount);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFeatureInputAtFn(
    IntPtr context,
    ulong entityId,
    ulong featureId,
    int index,
    out ulong inputId);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFeatureParamCountFn(IntPtr context, ulong entityId, ulong featureId);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostFeatureParamAtFn(
    IntPtr context,
    ulong entityId,
    ulong featureId,
    int index,
    IntPtr nameUtf8,
    int cap,
    out double value);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostBeginTransactionFn(IntPtr context, IntPtr nameUtf8);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostCommitTransactionFn(IntPtr context);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostAbortTransactionFn(IntPtr context);

[UnmanagedFunctionPointer(CallingConvention.Cdecl)]
public delegate int HostUnregisterPluginFn(IntPtr context, IntPtr pluginIdUtf8);

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
    public IntPtr SetSelection;
    public IntPtr ShowDialog;
    // v6：特征树 + 参数的只读面。
    public IntPtr EntityFeatureCount;
    public IntPtr EntityFeatureAt;
    public IntPtr FeatureInputAt;
    public IntPtr FeatureParamCount;
    public IntPtr FeatureParamAt;
    // v7：事务（批量编辑合成一条撤销记录）。
    public IntPtr BeginTransaction;
    public IntPtr CommitTransaction;
    public IntPtr AbortTransaction;
    // v8：摘掉一个扩展（重载时先把旧的摘下来）。
    public IntPtr UnregisterPlugin;
}
