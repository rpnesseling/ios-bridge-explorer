namespace IOSBridgeExplorer.UI.Models;

public sealed class FileEntry
{
    public required string Path { get; init; }
    public required string Name { get; init; }
    public required bool IsDirectory { get; init; }
    public required ulong SizeBytes { get; init; }
    public required DateTimeOffset ModifiedAt { get; init; }
    public string Type => IsDirectory ? "Dir" : "File";
}

