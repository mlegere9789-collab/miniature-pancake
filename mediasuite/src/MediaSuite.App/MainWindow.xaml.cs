using System.Windows;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
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
        Closing += OnClosing;
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
    /// Closing the app kills every running job's process outright (see
    /// <c>JobQueueManager.Dispose</c>) — a video half-encoded, an upscale most of the way
    /// through — with no way to resume it. Any other serious converter warns before
    /// throwing that away; this never did. Confirms first when anything is still running
    /// or queued, then saves the window's bounds either way (canceling the close doesn't
    /// need to skip that — nothing about the window's position changed by asking).
    /// </summary>
    private void OnClosing(object? sender, System.ComponentModel.CancelEventArgs e)
    {
        if (DataContext is MainViewModel { Queue.Rows: var rows }
            && rows.Any(row => !row.Job.IsFinished))
        {
            var running = rows.Count(row => !row.Job.IsFinished);
            var noun = running == 1 ? "job is" : "jobs are";

            var choice = MessageBox.Show(
                $"{running} {noun} still running or queued. Closing MediaSuite now will cancel "
                + "them — anything in progress will be lost.\n\nClose anyway?",
                "MediaSuite",
                MessageBoxButton.YesNo,
                MessageBoxImage.Warning,
                MessageBoxResult.No);

            if (choice != MessageBoxResult.Yes)
            {
                e.Cancel = true;
                return;
            }
        }

        SaveBoundsToSettings();
    }

    /// <summary>
    /// <see cref="Window.Left"/>/<see cref="Window.Top"/>/<see cref="Window.Width"/>/
    /// <see cref="Window.Height"/> reflect the maximized envelope while maximized, not the
    /// size to restore to — <see cref="Window.RestoreBounds"/> is the pre-maximize
    /// rectangle WPF already tracks for exactly this, so that is what gets saved whenever
    /// the window is currently maximized or minimized (which carries whatever it was
    /// before minimizing, same idea).
    /// </summary>
    private void SaveBoundsToSettings()
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
