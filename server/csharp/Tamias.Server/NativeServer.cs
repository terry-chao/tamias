using System.Runtime.InteropServices;

namespace Tamias.Server;

// src/server/server_api.h 的 P/Invoke 面。
//
// 约定（与头文件一一对应，改一边必须改另一边）：
//   - 返回 int：0 = OK，正数 = 错误码；tamias_session_can_* 是三态（1 / 0 / 负数 = 错）；
//   - 取字符串：原生侧写 UTF-8 + '\0'，返回写入字节数（不含结尾 0），cap 不够就截断；
//   - 失败原因用 LastError() 取：**原生侧是线程局部的**，所以必须在同一个线程上、
//     紧接着失败的那次调用之后读——不能攒起来回头再读。
//
// 调用约定：x64 上只有一种，写 Cdecl 只是为了和头文件里的 `extern "C"` 意图对齐。
internal static class NativeServer
{
    // 按名字解析：Windows 找 tamias_server.dll，Linux 找 libtamias_server.so。
    // 发布时原生库就摆在本程序同目录（cmake/TamiasDotnet.cmake 的 tamias_publish_server），
    // 所以不需要额外的 DllImportResolver。
    private const string Library = "tamias_server";

    // 错误码：与 server_api.h 的 enum 同值。改一边必须改另一边。
    public const int Ok = 0;
    public const int Failed = 1;
    public const int BadArg = 2;
    public const int BadHandle = 3;
    public const int Unsupported = 4;

    // 与 TAMIAS_SERVER_ABI_VERSION 同步。启动时握手，对不上拒绝启动。
    public const int ExpectedAbiVersion = 1;

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tamias_server_version();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_server_abi_version();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tamias_server_last_error();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_server_init();

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern IntPtr tamias_session_create(
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? documentNameUtf8);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern void tamias_session_destroy(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_dispatch(
        IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string commandUtf8,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string? argsUtf8);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_begin_transaction(
        IntPtr session, [MarshalAs(UnmanagedType.LPUTF8Str)] string? nameUtf8);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_commit_transaction(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_abort_transaction(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_undo(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_redo(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_can_undo(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_can_redo(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_document_name(IntPtr session, byte[] utf8, int cap);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_entity_count(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_entity_id_at(IntPtr session, int index, out ulong id);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_entity_name(IntPtr session, ulong id, byte[] utf8, int cap);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_entity_kind(IntPtr session, ulong id, byte[] utf8, int cap);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_selection_count(IntPtr session);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_selection_id_at(IntPtr session, int index, out ulong id);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_set_selection(IntPtr session, ulong[] ids, int count);

    [DllImport(Library, CallingConvention = CallingConvention.Cdecl)]
    private static extern int tamias_session_load_bytes(
        IntPtr session,
        [MarshalAs(UnmanagedType.LPUTF8Str)] string nameUtf8,
        byte[] data,
        int size);

    // ── 托管侧的薄包装 ────────────────────────────────────────────────────────

    /// <summary>原生库版本串（含 ABI 号，给人看的）。</summary>
    public static string Version => Marshal.PtrToStringUTF8(tamias_server_version()) ?? "";

    /// <summary>原生侧的 TAMIAS_SERVER_ABI_VERSION，启动握手用。</summary>
    public static int AbiVersion => tamias_server_abi_version();

    /// <summary>最近一次失败的原文，仅在同一线程上、紧接着失败调用之后有意义。</summary>
    public static string LastError => Marshal.PtrToStringUTF8(tamias_server_last_error()) ?? "";

    /// <summary>进程级初始化（幂等）。建会话时原生侧也会自动调，这里显式调一次好报错。</summary>
    public static void Init()
    {
        var rc = tamias_server_init();
        if (rc != Ok)
        {
            throw new ServerException(rc, LastError);
        }
    }

    public static IntPtr CreateSession(string? documentName)
    {
        var handle = tamias_session_create(documentName);
        if (handle == IntPtr.Zero)
        {
            throw new ServerException(Failed, $"tamias_session_create failed: {LastError}");
        }
        return handle;
    }

    public static void DestroySession(IntPtr handle) => tamias_session_destroy(handle);

    /// <summary>把原生返回码翻译成异常；成功返回 null。</summary>
    public static ServerException? Check(int rc)
        => rc == Ok ? null : new ServerException(rc, LastError);

    public static int Dispatch(IntPtr handle, string command, string? args)
        => tamias_session_dispatch(handle, command, args);

    public static int BeginTransaction(IntPtr handle, string? name)
        => tamias_session_begin_transaction(handle, name);

    public static int CommitTransaction(IntPtr handle) => tamias_session_commit_transaction(handle);

    public static int AbortTransaction(IntPtr handle) => tamias_session_abort_transaction(handle);

    public static int Undo(IntPtr handle) => tamias_session_undo(handle);

    public static int Redo(IntPtr handle) => tamias_session_redo(handle);

    public static int CanUndo(IntPtr handle) => tamias_session_can_undo(handle);

    public static int CanRedo(IntPtr handle) => tamias_session_can_redo(handle);

    public static int EntityCount(IntPtr handle) => tamias_session_entity_count(handle);

    public static int EntityIdAt(IntPtr handle, int index, out ulong id)
        => tamias_session_entity_id_at(handle, index, out id);

    public static int EntityName(IntPtr handle, ulong id, byte[] buffer, int cap)
        => tamias_session_entity_name(handle, id, buffer, cap);

    public static int EntityKind(IntPtr handle, ulong id, byte[] buffer, int cap)
        => tamias_session_entity_kind(handle, id, buffer, cap);

    public static int SelectionCount(IntPtr handle) => tamias_session_selection_count(handle);

    public static int SelectionIdAt(IntPtr handle, int index, out ulong id)
        => tamias_session_selection_id_at(handle, index, out id);

    public static int SetSelection(IntPtr handle, ulong[] ids, int count)
        => tamias_session_set_selection(handle, ids, count);

    public static int LoadBytes(IntPtr handle, string name, byte[] data, int size)
        => tamias_session_load_bytes(handle, name, data, size);

    public static int DocumentName(IntPtr handle, byte[] buffer, int cap)
        => tamias_session_document_name(handle, buffer, cap);
}
