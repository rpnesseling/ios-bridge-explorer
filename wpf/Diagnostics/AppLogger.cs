using System.Text;
using System.IO;

namespace IOSBridgeExplorer.UI.Diagnostics;

internal static class AppLogger
{
    private static readonly object Sync = new();
    private static readonly string LogDirectory = Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
        "ios-bridge-explorer",
        "logs");

    internal static string LogPath { get; } = Path.Combine(LogDirectory, "app.log");

    internal static void Info(string message) => Write("INFO", message, null);

    internal static void Error(string message, Exception? ex = null) => Write("ERROR", message, ex);

    private static void Write(string level, string message, Exception? ex)
    {
        try
        {
            lock (Sync)
            {
                Directory.CreateDirectory(LogDirectory);
                var sb = new StringBuilder();
                sb.Append('[').Append(DateTimeOffset.Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz")).Append(']');
                sb.Append(' ').Append(level).Append(' ').Append(message);
                sb.AppendLine();
                if (ex is not null)
                {
                    sb.AppendLine(ex.ToString());
                }

                File.AppendAllText(LogPath, sb.ToString(), Encoding.UTF8);
            }
        }
        catch
        {
            // Logging must not crash the app.
        }
    }
}

