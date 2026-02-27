using System.Runtime.InteropServices;
using System.Text;

namespace IOSBridgeExplorer.UI.Interop;

internal static class NativeMethods
{
    private const string DllName = "ios_device_bridge.dll";

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct DeviceInfoNative
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 64)]
        public string Udid;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Name;
    }

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
    internal struct FileEntryNative
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 512)]
        public string Path;

        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 128)]
        public string Name;

        public int IsDirectory;
        public ulong SizeBytes;
        public long ModifiedUnix;
    }

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_get_version(StringBuilder buffer, int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_get_last_error(StringBuilder buffer, int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_get_runtime_diagnostics(StringBuilder buffer, int bufferSize);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_enumerate_devices([Out] DeviceInfoNative[]? outDevices, int maxDevices);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_open_device(string udid, out int outHandle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl)]
    internal static extern int iosb_close_device(int handle);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_list_directory(int handle, string path, [Out] FileEntryNative[]? outEntries, int maxEntries);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_pull_file(int handle, string remotePath, string localPath);

    [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
    internal static extern int iosb_push_file(int handle, string localPath, string remotePath);

    internal static string LastError()
    {
        var buffer = new StringBuilder(1024);
        var ok = iosb_get_last_error(buffer, buffer.Capacity);
        return ok == 1 ? buffer.ToString() : "Native call failed";
    }

    internal static string RuntimeDiagnostics()
    {
        var buffer = new StringBuilder(8192);
        var ok = iosb_get_runtime_diagnostics(buffer, buffer.Capacity);
        return ok == 1 ? buffer.ToString() : LastError();
    }
}

