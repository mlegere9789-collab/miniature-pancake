using System.Windows;
using System.Windows.Threading;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Engines;
using MediaSuite.Core.Formats;
using MediaSuite.Core.GoogleDrive;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using MediaSuite.Core.Updates;

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
    private GoogleDriveClient? _driveClient;
    private GitHubReleaseUpdateChecker? _updateChecker;
    private SingleInstanceGuard? _singleInstance;
    private MainWindow? _window;

    protected override void OnStartup(StartupEventArgs e)
    {
        base.OnStartup(e);

        var openedFiles = ResolveOpenWithFiles(e.Args);

        var singleInstance = SingleInstanceGuard.Acquire();
        _singleInstance = singleInstance;
        if (!singleInstance.IsFirstInstance)
        {
            // A window is already open somewhere — hand this launch's file off to it and
            // stop immediately, before any of the real startup work below (settings load,
            // tool discovery, queue, Drive client) that a second window would otherwise
            // duplicate for nothing.
            SingleInstanceGuard.ForwardToRunningInstance(openedFiles);
            singleInstance.Dispose();
            _singleInstance = null;
            Shutdown();
            return;
        }

        // Started this early, not after the window is up: everything below (settings
        // load, tool discovery, engine registry, Drive client) is real I/O that can take
        // a visible moment, and a second launch racing in during that window would
        // otherwise find nobody listening yet. OnFilesForwardedFromAnotherLaunch already
        // tolerates _window/_mainViewModel still being null this early.
        singleInstance.StartListening(OnFilesForwardedFromAnotherLaunch);

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

        // Every engine the app has so far. Later build steps add video, PDF, document
        // and upscaling engines to the same registry.
        var processRunner = new ProcessRunner();
        var engines = EngineSetup.CreateDefaultRegistry(processRunner, toolLocator);

        _driveClient = new GoogleDriveClient(settings);
        _updateChecker = new GitHubReleaseUpdateChecker();

        _queue = new JobQueueManager(engines, workspaces, settings.MaxConcurrentJobs, toolLocator, _driveClient);
        var launcher = new JobLauncher(_queue, settings);

        _mainViewModel = new MainViewModel(
            settings, store, _themeService, toolLocator, _queue, engines, launcher, _driveClient, _updateChecker, Dispatcher);

        if (openedFiles.Count > 0)
        {
            _mainViewModel.OpenWithFiles(openedFiles);
        }

        _window = new MainWindow(_themeService, settings, store)
        {
            DataContext = _mainViewModel,
        };

        this.MainWindow = _window;
        _window.Show();
    }

    /// <summary>
    /// Runs on a background thread inside <see cref="SingleInstanceGuard"/> whenever a
    /// second launch hands this instance its files instead of opening its own window.
    /// Brings the existing window to the front regardless of whether any files came with
    /// it — a plain second double-click with nothing to open should still surface the
    /// window rather than silently do nothing.
    /// </summary>
    private void OnFilesForwardedFromAnotherLaunch(IReadOnlyList<string> files)
    {
        Dispatcher.Invoke(() =>
        {
            if (files.Count > 0)
            {
                _mainViewModel?.OpenWithFiles(files);
            }

            if (_window is null)
            {
                return;
            }

            if (_window.WindowState == WindowState.Minimized)
            {
                _window.WindowState = WindowState.Normal;
            }

            _window.Activate();
        });
    }

    /// <summary>
    /// Windows puts a file path on the command line for "Open with MediaSuite" (see the
    /// installer's <c>SupportedTypes</c> registration) and for a file dragged onto the
    /// exe or a shortcut to it — either way, this app is being told to do something with
    /// a specific file, not just launched idle. Filters to paths the format catalogue
    /// actually recognises, so a file passed this way that the app can never act on lands
    /// nowhere silently rather than showing up staged for an operation it can't complete.
    /// </summary>
    private static IReadOnlyList<string> ResolveOpenWithFiles(string[] args) =>
        InputCollector.Expand(args).Where(file => FormatCatalog.FromPath(file) is not null).ToList();

    protected override void OnExit(ExitEventArgs e)
    {
        // Cancels anything still running and lets each job delete its scratch folder.
        _queue?.Dispose();
        _mainViewModel?.Dispose();
        _themeService?.Dispose();
        _driveClient?.Dispose();
        _updateChecker?.Dispose();
        _singleInstance?.Dispose();
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
