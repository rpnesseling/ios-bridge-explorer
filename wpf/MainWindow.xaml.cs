using IOSBridgeExplorer.UI.ViewModels;
using System.Windows;
using System.Windows.Input;

namespace IOSBridgeExplorer.UI;

public partial class MainWindow : Window
{
    private readonly MainViewModel _vm;

    public MainWindow()
    {
        InitializeComponent();
        _vm = new MainViewModel();
        DataContext = _vm;
    }

    private void FileList_MouseDoubleClick(object sender, MouseButtonEventArgs e)
    {
        _vm.HandleEntryDoubleClick();
    }

    protected override void OnClosed(EventArgs e)
    {
        base.OnClosed(e);
        _vm.Dispose();
    }
}

