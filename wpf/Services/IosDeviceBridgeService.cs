using IOSBridgeExplorer.UI.Diagnostics;
using IOSBridgeExplorer.UI.Interop;
using IOSBridgeExplorer.UI.Models;
using System.Text;

namespace IOSBridgeExplorer.UI.Services;

public sealed class IosDeviceBridgeService : IIosDeviceService
{
    private const long MinUnixSeconds = -62135596800L;
    private const long MaxUnixSeconds = 253402300799L;
    private int _deviceHandle = -1;

    public string GetVersion()
    {
        var sb = new StringBuilder(256);
        var rc = NativeMethods.iosb_get_version(sb, sb.Capacity);
        if (rc != 1)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_get_version failed rc={rc}: {error}");
            throw new InvalidOperationException(error);
        }
        return sb.ToString();
    }

    public string GetRuntimeDiagnostics()
    {
        return NativeMethods.RuntimeDiagnostics();
    }

    public IReadOnlyList<DeviceInfo> EnumerateDevices()
    {
        var count = NativeMethods.iosb_enumerate_devices(null, 0);
        if (count < 0)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_enumerate_devices(count) failed rc={count}: {error}");
            throw new InvalidOperationException(error);
        }
        if (count == 0)
        {
            AppLogger.Info("iosb_enumerate_devices returned 0 devices.");
            return Array.Empty<DeviceInfo>();
        }

        var buffer = new NativeMethods.DeviceInfoNative[count];
        var written = NativeMethods.iosb_enumerate_devices(buffer, buffer.Length);
        if (written < 0)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_enumerate_devices(fill) failed rc={written}: {error}");
            throw new InvalidOperationException(error);
        }

        AppLogger.Info($"iosb_enumerate_devices succeeded: written={written}");
        return buffer.Take(written).Select(d => new DeviceInfo
        {
            Udid = d.Udid,
            Name = d.Name
        }).ToArray();
    }

    public void Connect(string udid)
    {
        Disconnect();
        var rc = NativeMethods.iosb_open_device(udid, out _deviceHandle);
        if (rc != 1)
        {
            _deviceHandle = -1;
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_open_device failed rc={rc} udid={udid}: {error}");
            throw new InvalidOperationException(error);
        }
        AppLogger.Info($"iosb_open_device succeeded handle={_deviceHandle} udid={udid}");
    }

    public void Disconnect()
    {
        if (_deviceHandle > 0)
        {
            NativeMethods.iosb_close_device(_deviceHandle);
            _deviceHandle = -1;
        }
    }

    public IReadOnlyList<FileEntry> ListDirectory(string path)
    {
        if (_deviceHandle <= 0)
        {
            throw new InvalidOperationException("No connected device.");
        }

        var count = NativeMethods.iosb_list_directory(_deviceHandle, path, null, 0);
        if (count < 0)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_list_directory(count) failed rc={count} handle={_deviceHandle} path={path}: {error}");
            throw new InvalidOperationException(error);
        }
        if (count == 0)
        {
            return Array.Empty<FileEntry>();
        }

        var buffer = new NativeMethods.FileEntryNative[count];
        var written = NativeMethods.iosb_list_directory(_deviceHandle, path, buffer, buffer.Length);
        if (written < 0)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_list_directory(fill) failed rc={written} handle={_deviceHandle} path={path}: {error}");
            throw new InvalidOperationException(error);
        }

        return buffer.Take(written).Select(x => new FileEntry
        {
            Path = x.Path,
            Name = x.Name,
            IsDirectory = x.IsDirectory == 1,
            SizeBytes = x.SizeBytes,
            ModifiedAt = SafeFromUnixTime(x.ModifiedUnix)
        }).OrderByDescending(x => x.IsDirectory).ThenBy(x => x.Name).ToArray();
    }

    private static DateTimeOffset SafeFromUnixTime(long raw)
    {
        var seconds = raw;

        // AFC backends sometimes return mtime in ms/us/ns; normalize to seconds.
        while (seconds > MaxUnixSeconds || seconds < MinUnixSeconds)
        {
            seconds /= 1000;
            if (seconds == 0)
            {
                break;
            }
        }

        if (seconds > MaxUnixSeconds)
        {
            seconds = MaxUnixSeconds;
        }
        else if (seconds < MinUnixSeconds)
        {
            seconds = MinUnixSeconds;
        }

        return DateTimeOffset.FromUnixTimeSeconds(seconds);
    }

    public void PullFile(string remotePath, string localPath)
    {
        if (_deviceHandle <= 0)
        {
            throw new InvalidOperationException("No connected device.");
        }
        var rc = NativeMethods.iosb_pull_file(_deviceHandle, remotePath, localPath);
        if (rc != 1)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_pull_file failed rc={rc} handle={_deviceHandle} remote={remotePath} local={localPath}: {error}");
            throw new InvalidOperationException(error);
        }
    }

    public void PushFile(string localPath, string remotePath)
    {
        if (_deviceHandle <= 0)
        {
            throw new InvalidOperationException("No connected device.");
        }
        var rc = NativeMethods.iosb_push_file(_deviceHandle, localPath, remotePath);
        if (rc != 1)
        {
            var error = NativeMethods.LastError();
            AppLogger.Error($"iosb_push_file failed rc={rc} handle={_deviceHandle} local={localPath} remote={remotePath}: {error}");
            throw new InvalidOperationException(error);
        }
    }

    public void Dispose()
    {
        Disconnect();
    }
}

