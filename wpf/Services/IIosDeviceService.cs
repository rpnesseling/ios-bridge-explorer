using IOSBridgeExplorer.UI.Models;

namespace IOSBridgeExplorer.UI.Services;

public interface IIosDeviceService : IDisposable
{
    string GetVersion();
    string GetRuntimeDiagnostics();
    IReadOnlyList<DeviceInfo> EnumerateDevices();
    void Connect(string udid);
    void Disconnect();
    IReadOnlyList<FileEntry> ListDirectory(string path);
    void PullFile(string remotePath, string localPath);
    void PushFile(string localPath, string remotePath);
}

