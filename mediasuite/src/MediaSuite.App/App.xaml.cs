using System.Windows;
using System.Windows.Threading;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;

namespace MediaSuite.App;

/// <summary>
/// Application entry point: load settings, apply the theme before anything is shown,
/// discover the bundled tools, then open the shell.
/// </summary>
public partial class App : Application
{
    private ThemeService? _themeService;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        DispatcherUnhandledException += OnDispatcherUnhandledException;

        var store = new JsonSettingsStore();
        var settings = store.Load();

        _themeService = new ThemeService();
        _themeService.Apply(settings.Theme);

        var toolLocator = ToolLocator.ForApplication(
            AppContext.BaseDirectory,
            settings.ToolsDirectory,
            BuildOverrides(settings));

        var window = new MainWindow(_themeService)
        {
            DataContext = new MainViewModel(settings, store, _themeService, toolLocator),
        };

        this.MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        _themeService?.Dispose();
        base.OnExit(e);
    }

    private static Dictionary<ExternalToolId, string> BuildOverrides(AppSettings settings)
    {
        var overrides = new Dictionary<ExternalToolId, string>();

        foreach (var (key, path) in settings.ToolPathOverrides)
        {
            if (Enum.TryParse<ExternalToolId>(key, ignoreCase: true, out var id))
            {
                overrides[id] = path;
            }
        }

        return overrides;
    }

    private void OnDispatcherUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
    {
        // Keep the app alive: one failed screen should never take the whole session with it.
        MessageBox.Show(
            $"Something went wrong:\n\n{e.Exception.Message}",
            "MediaSuite",
            MessageBoxButton.OK,
            MessageBoxImage.Warning);

        e.Handled = true;
    }
}
