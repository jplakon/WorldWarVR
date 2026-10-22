using Microsoft.UI.Xaml;
using Microsoft.UI.Windowing;
using Windows.Graphics;
using Windows.UI.ViewManagement;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace WorldAtWarVR_Launcher;

/// <summary>
/// The application window. This hosts a Frame that displays pages. Add your
/// UI and logic to MainPage.xaml / MainPage.xaml.cs instead of here so you
/// can use Page features such as navigation events and the Loaded lifecycle.
/// </summary>
public sealed partial class MainWindow : Window
{
    private const double DesignWidth = 1440.0;
    private const double DesignHeight = 970.0;
    private const double DesignTitleBarHeight = 116.0;
    private const double MinimumScale = 0.70;

    public MainWindow()
    {
        InitializeComponent();

        if (new AccessibilitySettings().HighContrast)
        {
            BackgroundTexture.Visibility = Visibility.Collapsed;
            BackgroundTint.Visibility = Visibility.Collapsed;
            WindowRoot.Background = (Microsoft.UI.Xaml.Media.Brush)
                Application.Current.Resources["LauncherBaseBrush"];
        }

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(TitleBarDragRegion);

        AppWindow.SetIcon(Path.Combine(
            AppContext.BaseDirectory, "Assets", "WorldWarVR-Icon.ico"));
        AppWindow.ResizeClient(new SizeInt32((int)DesignWidth, (int)DesignHeight));

        if (AppWindow.Presenter is OverlappedPresenter presenter)
        {
            presenter.PreferredMinimumWidth = (int)Math.Round(DesignWidth * MinimumScale);
            presenter.PreferredMinimumHeight = (int)Math.Round(DesignHeight * MinimumScale);
        }

        var titleBar = AppWindow.TitleBar;
        titleBar.BackgroundColor = Microsoft.UI.Colors.Transparent;
        titleBar.ForegroundColor = Windows.UI.Color.FromArgb(255, 222, 213, 195);
        titleBar.InactiveBackgroundColor = Microsoft.UI.Colors.Transparent;
        titleBar.InactiveForegroundColor = Windows.UI.Color.FromArgb(255, 126, 121, 112);
        titleBar.ButtonBackgroundColor = Microsoft.UI.Colors.Transparent;
        titleBar.ButtonForegroundColor = Windows.UI.Color.FromArgb(255, 222, 213, 195);
        titleBar.ButtonHoverBackgroundColor = Windows.UI.Color.FromArgb(255, 49, 45, 40);
        titleBar.ButtonHoverForegroundColor = Windows.UI.Color.FromArgb(255, 255, 247, 232);
        titleBar.ButtonPressedBackgroundColor = Windows.UI.Color.FromArgb(255, 83, 65, 45);
        titleBar.ButtonPressedForegroundColor = Microsoft.UI.Colors.White;
        titleBar.ButtonInactiveBackgroundColor = Microsoft.UI.Colors.Transparent;
        titleBar.ButtonInactiveForegroundColor = Windows.UI.Color.FromArgb(255, 126, 121, 112);

        var displayArea = DisplayArea.GetFromWindowId(AppWindow.Id, DisplayAreaFallback.Primary);
        if (displayArea is not null)
        {
            var workArea = displayArea.WorkArea;
            var x = workArea.X + Math.Max(0, (workArea.Width - AppWindow.Size.Width) / 2);
            var y = workArea.Y + Math.Max(0, (workArea.Height - AppWindow.Size.Height) / 2);
            AppWindow.Move(new PointInt32(x, y));
        }

        RootFrame.Navigate(typeof(MainPage));
    }

    private void WindowRoot_SizeChanged(object sender, SizeChangedEventArgs e)
    {
        var scale = Math.Min(
            1.0,
            Math.Min(e.NewSize.Width / DesignWidth, e.NewSize.Height / DesignHeight));

        TitleBarDragRegion.Width = e.NewSize.Width;
        TitleBarDragRegion.Height = Math.Max(1.0, DesignTitleBarHeight * scale);
    }
}
