using IOSBridgeExplorer.UI.Diagnostics;
using IOSBridgeExplorer.UI.Models;
using IOSBridgeExplorer.UI.Services;
using System.Collections.ObjectModel;
using System.Diagnostics;
using System.IO;
using System.Windows;
using System.Windows.Input;

namespace IOSBridgeExplorer.UI.ViewModels;

public sealed class MainViewModel : ViewModelBase, IDisposable
{
    private readonly IIosDeviceService _service;
    private DeviceInfo? _selectedDevice;
    private FileEntry? _selectedEntry;
    private string _currentPath = "/";
    private string _statusText = "Ready";

    public MainViewModel()
        : this(new IosDeviceBridgeService())
    {
    }

    public MainViewModel(IIosDeviceService service)
    {
        _service = service;

        RefreshDevicesCommand = new RelayCommand(RefreshDevices);
        DiagnosticsCommand = new RelayCommand(ShowDiagnostics);
        OpenLogCommand = new RelayCommand(OpenLogFile);
        ConnectCommand = new RelayCommand(ConnectDevice, () => SelectedDevice is not null);
        OpenCommand = new RelayCommand(OpenSelected, () => SelectedEntry?.IsDirectory == true);
        UpCommand = new RelayCommand(GoUp, () => CurrentPath != "/");
        RefreshDirectoryCommand = new RelayCommand(RefreshDirectory, () => SelectedDevice is not null);

        try
        {
            StatusText = $"Bridge: {_service.GetVersion()}";
            AppLogger.Info($"Startup bridge version: {StatusText}");
            AppLogger.Info($"Log file: {AppLogger.LogPath}");
            StatusText = "Ready. Click Diagnostics, then Refresh Devices.";
        }
        catch (Exception ex)
        {
            StatusText = $"Startup error: {ex.Message}";
            AppLogger.Error("Startup failed.", ex);
        }
    }

    public ObservableCollection<DeviceInfo> Devices { get; } = new();
    public ObservableCollection<FileEntry> Entries { get; } = new();

    public DeviceInfo? SelectedDevice
    {
        get => _selectedDevice;
        set
        {
            _selectedDevice = value;
            Raise();
            RaiseCommandStates();
        }
    }

    public FileEntry? SelectedEntry
    {
        get => _selectedEntry;
        set
        {
            _selectedEntry = value;
            Raise();
            RaiseCommandStates();
        }
    }

    public string CurrentPath
    {
        get => _currentPath;
        set
        {
            _currentPath = value;
            Raise();
            RaiseCommandStates();
        }
    }

    public string StatusText
    {
        get => _statusText;
        set
        {
            _statusText = value;
            Raise();
        }
    }

    public ICommand RefreshDevicesCommand { get; }
    public ICommand DiagnosticsCommand { get; }
    public ICommand OpenLogCommand { get; }
    public ICommand ConnectCommand { get; }
    public ICommand OpenCommand { get; }
    public ICommand UpCommand { get; }
    public ICommand RefreshDirectoryCommand { get; }

    public void HandleEntryDoubleClick()
    {
        if (SelectedEntry?.IsDirectory == true)
        {
            OpenSelected();
        }
    }

    private void RefreshDevices()
    {
        try
        {
            Devices.Clear();
            foreach (var device in _service.EnumerateDevices())
            {
                Devices.Add(device);
            }
            StatusText = $"Found {Devices.Count} device(s)";
        }
        catch (Exception ex)
        {
            StatusText = $"Device refresh failed: {ex.Message}";
            AppLogger.Error("RefreshDevices failed.", ex);
            MessageBox.Show(ex.Message, "Device error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void ShowDiagnostics()
    {
        try
        {
            var diagnostics = _service.GetRuntimeDiagnostics();
            StatusText = "Runtime diagnostics generated";
            MessageBox.Show(diagnostics, "Native Runtime Diagnostics", MessageBoxButton.OK, MessageBoxImage.Information);
        }
        catch (Exception ex)
        {
            StatusText = $"Diagnostics failed: {ex.Message}";
            MessageBox.Show(ex.Message, "Diagnostics error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void ConnectDevice()
    {
        if (SelectedDevice is null)
        {
            return;
        }

        try
        {
            _service.Connect(SelectedDevice.Udid);
            CurrentPath = "/";
            RefreshDirectory();
            StatusText = $"Connected: {SelectedDevice.Name}";
        }
        catch (Exception ex)
        {
            StatusText = $"Connect failed: {ex.Message}";
            AppLogger.Error("ConnectDevice failed.", ex);
            MessageBox.Show(ex.Message, "Connection error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OpenSelected()
    {
        if (SelectedEntry?.IsDirectory != true)
        {
            return;
        }

        CurrentPath = SelectedEntry.Path;
        RefreshDirectory();
    }

    private void GoUp()
    {
        if (CurrentPath == "/")
        {
            return;
        }

        var trimmed = CurrentPath.TrimEnd('/');
        var index = trimmed.LastIndexOf('/');
        CurrentPath = index <= 0 ? "/" : trimmed[..index];
        RefreshDirectory();
    }

    private void RefreshDirectory()
    {
        try
        {
            Entries.Clear();
            foreach (var entry in _service.ListDirectory(CurrentPath))
            {
                Entries.Add(entry);
            }
            StatusText = $"Path: {CurrentPath} ({Entries.Count} entries)";
        }
        catch (Exception ex)
        {
            StatusText = $"List failed: {ex.Message}";
            AppLogger.Error($"RefreshDirectory failed. path={CurrentPath}", ex);
            MessageBox.Show(ex.Message, "Browse error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void OpenLogFile()
    {
        try
        {
            var logPath = AppLogger.LogPath;
            if (!File.Exists(logPath))
            {
                AppLogger.Info("OpenLog requested; log file does not exist yet.");
                MessageBox.Show($"Log file not found yet:\n{logPath}", "Open Log", MessageBoxButton.OK, MessageBoxImage.Information);
                return;
            }

            var psi = new ProcessStartInfo
            {
                FileName = "notepad.exe",
                Arguments = $"\"{logPath}\"",
                UseShellExecute = true
            };
            Process.Start(psi);
        }
        catch (Exception ex)
        {
            StatusText = $"Open log failed: {ex.Message}";
            AppLogger.Error("OpenLogFile failed.", ex);
            MessageBox.Show(ex.Message, "Open Log error", MessageBoxButton.OK, MessageBoxImage.Error);
        }
    }

    private void RaiseCommandStates()
    {
        ((RelayCommand)ConnectCommand).RaiseCanExecuteChanged();
        ((RelayCommand)OpenCommand).RaiseCanExecuteChanged();
        ((RelayCommand)UpCommand).RaiseCanExecuteChanged();
        ((RelayCommand)RefreshDirectoryCommand).RaiseCanExecuteChanged();
    }

    public void Dispose()
    {
        _service.Dispose();
    }
}

