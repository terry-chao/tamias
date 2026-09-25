using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Tamias.Api;

namespace Tamias.Host;

public static class Bootstrap
{
    [StructLayout(LayoutKind.Sequential)]
    struct NativePickPoint
    {
        public float X;
        public float Y;
        public float Z;
        public uint Reserved;
        public ulong EntityId;
    }

    static Host? host_;

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int Initialize(IntPtr apiPtr, IntPtr pluginsDirUtf8)
    {
        try
        {
            var api = Marshal.PtrToStructure<HostApi>(apiPtr);
            if (api.AbiVersion != HostApiVersion.Current)
            {
                return -2;
            }
            host_ = new Host(api);
            PluginLoader.LoadAll(host_, Marshal.PtrToStringUTF8(pluginsDirUtf8));
            return 0;
        }
        catch (Exception ex)
        {
            try
            {
                host_?.Log(ex.ToString());
            }
            catch
            {
                // Host may not be usable yet.
            }
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int Shutdown()
    {
        host_?.Detach();
        host_ = null;
        return 0;
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int Invoke(IntPtr commandIdUtf8)
    {
        try
        {
            if (host_ == null)
            {
                return -1;
            }
            var id = Marshal.PtrToStringUTF8(commandIdUtf8);
            if (string.IsNullOrEmpty(id))
            {
                return -1;
            }
            if (!host_.Actions.TryGetValue(id, out var action))
            {
                host_.Log("Unknown plugin command: " + id);
                return -1;
            }
            action();
            return 0;
        }
        catch (Exception ex)
        {
            host_?.Log(ex.Message);
            return -1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int PointInputCompleted(ulong requestId, IntPtr points, int count, int status)
    {
        try
        {
            if (host_ == null)
            {
                throw new InvalidOperationException("Host is not initialized");
            }
            if (count < 0 || (count > 0 && points == IntPtr.Zero))
            {
                throw new ArgumentException("Invalid point input result");
            }

            var result = new List<PickPoint>(count);
            var stride = Marshal.SizeOf<NativePickPoint>();
            for (var i = 0; i < count; ++i)
            {
                var native = Marshal.PtrToStructure<NativePickPoint>(
                    IntPtr.Add(points, checked(i * stride)));
                result.Add(new PickPoint(native.X, native.Y, native.Z, native.EntityId));
            }
            host_.CompletePointInput(requestId, result, status != 0);
            return 0;
        }
        catch (Exception ex)
        {
            try
            {
                host_?.Log(ex.ToString());
            }
            catch
            {
                // Host logging may not be available.
            }
            return -1;
        }
    }

    // 命令控制台的入口：求值一段 C# 片段。
    // 0 = 成功（缓冲里是结果，可能为空）、1 = 脚本报错（缓冲里是错误文本）、-1 = 宿主不可用。
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    public static int Evaluate(IntPtr codeUtf8, IntPtr outUtf8, int cap)
    {
        if (host_ == null)
        {
            WriteUtf8("C# host is not initialized", outUtf8, cap);
            return -1;
        }
        var code = Marshal.PtrToStringUTF8(codeUtf8);
        if (string.IsNullOrWhiteSpace(code))
        {
            WriteUtf8("", outUtf8, cap);
            return 0;
        }
        try
        {
            WriteUtf8(ScriptEngine.Evaluate(host_, code), outUtf8, cap);
            return 0;
        }
        catch (Exception ex)
        {
            WriteUtf8(ScriptEngine.DescribeException(ex), outUtf8, cap);
            return 1;
        }
    }

    static void WriteUtf8(string text, IntPtr buffer, int cap)
    {
        if (buffer == IntPtr.Zero || cap <= 0)
        {
            return;
        }
        var bytes = Encoding.UTF8.GetBytes(text);
        var count = Math.Min(bytes.Length, cap - 1);
        Marshal.Copy(bytes, 0, buffer, count);
        Marshal.WriteByte(buffer, count, 0);
    }
}
