using System.Windows;
using MediaSuite.App.Services;
using MediaSuite.Core.Settings;

namespace MediaSuite.App;

public partial class MainWindow : Window
{
    private readonly ThemeService _themeService;
    private readonly AppSettings _settings;
    private readonly ISettingsStore _store;

    public MainWindow(ThemeService themeService, AppSettings settings, ISettingsStore store)
    {
        _themeService = themeService;
        _settings = settings;
        _store = store;
        InitializeComponent();

        RestoreBoundsFromSettings();
        Closing += SaveBoundsToSettings;
    }

    protected override void OnSourceInitialized(EventArgs e)
    {
        base.OnSourceInitialized(e);

        // The window handle only exists from here on, which is what the dark title bar needs.
        _themeService.ApplyTitleBarTheme(this);
    }

    /// <summary>
    /// Applies a saved position and size, but only when at least part of that rectangle
    /// would still land on a currently connected monitor — a laptop docked to a second
    /// screen last time, now undocked, must not reopen an inaccessible off-screen window.
    /// Falls back to the XAML default (centered, 1280x820) whenever nothing was saved yet,
    /// or what was saved no longer fits anywhere real.
    /// </summary>
    private void RestoreBoundsFromSettings()
    {
        if (_settings.WindowWidth is not { } width || _settings.WindowHeight is not { } height
            || _settings.WindowLeft is not { } left || _settings.WindowTop is not { } top)
        {
            return;
        }

        var virtualScreen = new Rect(
            SystemParameters.VirtualScreenLeft,
            SystemParameters.VirtualScreenTop,
            SystemParameters.VirtualScreenWidth,
            SystemParameters.VirtualScreenHeight);

        if (!virtualScreen.IntersectsWith(new Rect(left, top, width, height)))
        {
            return;
        }

        WindowStartupLocation = WindowStartupLocation.Manual;
        Left = left;
        Top = top;
        Width = width;
        Height = height;

        if (_settings.WindowMaximized)
        {
            WindowState = WindowState.Maximized;
        }
    }

    /// <summary>
    /// <see cref="Window.Left"/>/<see cref="Window.Top"/>/<see cref="Window.Width"/>/
    /// <see cref="Window.Height"/> reflect the maximized envelope while maximized, not the
    /// size to restore to — <see cref="Window.RestoreBounds"/> is the pre-maximize
    /// rectangle WPF already tracks for exactly this, so that is what gets saved whenever
    /// the window is currently maximized or minimized (which carries whatever it was
    /// before minimizing, same idea).
    /// </summary>
    private void SaveBoundsToSettings(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        var bounds = WindowState == WindowState.Normal ? new Rect(Left, Top, Width, Height) : RestoreBounds;

        _settings.WindowLeft = bounds.Left;
        _settings.WindowTop = bounds.Top;
        _settings.WindowWidth = bounds.Width;
        _settings.WindowHeight = bounds.Height;
        _settings.WindowMaximized = WindowState == WindowState.Maximized;

        _store.Save(_settings);
    }
}
