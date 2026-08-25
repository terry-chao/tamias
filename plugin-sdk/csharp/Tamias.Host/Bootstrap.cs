using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
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
            if (api.AbiVersion != 4)
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
}
