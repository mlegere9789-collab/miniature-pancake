using System.Windows;
using System.Windows.Threading;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;

namespace MediaSuite.App;

/// <summary>
/// Application entry point: load settings, apply the theme before anything is shown,
/// discover the bundled tools, then open the shell.
/// </summary>
public partial class App : Application
{
    /// <summary>Scratch folders older than this are assumed to be crash leftovers.</summary>
    private static readonly TimeSpan StaleWorkspaceAge = TimeSpan.FromHours(12);

    private ThemeService? _themeService;
    private JobQueueManager? _queue;
    private MainViewModel? _mainViewModel;

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

        var workspaces = new DiskTempWorkspaceFactory(settings.ResolveTempDirectory());
        PurgeStaleWorkspaces(workspaces);

        // Engines register from build step 4 onwards; the queue is engine-agnostic and
        // simply reports "nothing can handle this yet" until they do.
        var engines = new EngineRegistry();

        _queue = new JobQueueManager(engines, workspaces, settings.MaxConcurrentJobs, toolLocator);
        _mainViewModel = new MainViewModel(settings, store, _themeService, toolLocator, _queue, Dispatcher);

        var window = new MainWindow(_themeService)
        {
            DataContext = _mainViewModel,
        };

        this.MainWindow = window;
        window.Show();
    }

    protected override void OnExit(ExitEventArgs e)
    {
        // Cancels anything still running and lets each job delete its scratch folder.
        _queue?.Dispose();
        _mainViewModel?.Dispose();
        _themeService?.Dispose();
        base.OnExit(e);
    }

    /// <summary>
    /// Clears scratch folders a previous session left behind. A crash mid-encode skips
    /// the normal cleanup, and those files can be very large.
    /// </summary>
    private static void PurgeStaleWorkspaces(DiskTempWorkspaceFactory workspaces)
    {
        try
        {
            workspaces.PurgeStaleWorkspaces(StaleWorkspaceAge);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            // Never let housekeeping stop the app from starting.
        }
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
