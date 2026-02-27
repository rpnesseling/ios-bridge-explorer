namespace IOSBridgeExplorer.UI.Models;

public sealed class DeviceInfo
{
    public required string Udid { get; init; }
    public required string Name { get; init; }
    public override string ToString() => $"{Name} ({Udid})";
}

