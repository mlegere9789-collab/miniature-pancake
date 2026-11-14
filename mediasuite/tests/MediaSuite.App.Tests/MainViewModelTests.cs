using System.IO;
using System.Windows.Threading;
using MediaSuite.App.Services;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using MediaSuite.Core.Updates;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// <see cref="MainViewModel"/> owns the navigation rail and wires the four module pages,
/// Settings and the live queue together — the real regression risk here is the wiring
/// itself: does flipping a Settings switch actually reach the queue and the already-open
/// module pages, does the dependency-warning banner reflect what is really installed,
/// does an "Open with" launch land on the right page. Driven through real
/// ModulePageViewModel/SettingsViewModel/JobQueueViewModel instances (no seam to fake any
/// of them individually), with only the outside-world dependencies (settings
/// persistence, Google Drive, update checks) faked.
/// </summary>
public sealed class MainViewModelTests : IDisposable
{
    private readonly string _workDir = Path.Combine(Path.GetTempPath(), "MediaSuite.App.Tests." + Guid.NewGuid().ToString("N"));
    private readonly Dispatcher _dispatcher = Dispatcher.CurrentDispatcher;

    public void Dispose()
    {
        if (Directory.Exists(_workDir))
        {
            Directory.Delete(_workDir, recursive: true);
        }
    }

    private sealed record Fixture(
        MainViewModel Main,
        JobQueueManager Queue,
        FakeSettingsStore Store,
        FakeGoogleDriveClient Drive,
        FakeUpdateCheckClient UpdateClient,
        ThemeService Theme) : IDisposable
    {
        public void Dispose()
        {
            Main.Dispose();
            Theme.Dispose();
        }
    }

    private Fixture CreateMain(
        FakeEngine engine,
        bool checkForUpdates = false,
        UpdateCheckResult? updateResult = null,
        IReadOnlyList<string>? toolSearchRoots = null)
    {
        var settings = new AppSettings { CheckForUpdatesOnLaunch = checkForUpdates };
        var store = new FakeSettingsStore(settings);
        var drive = new FakeGoogleDriveClient();
        var theme = new ThemeService();
        var toolLocator = new ToolLocator(toolSearchRoots ?? Array.Empty<string>(), pathVariable: string.Empty);
        var registry = new EngineRegistry().Register(engine);
        var queue = new JobQueueManager(registry, new DiskTempWorkspaceFactory(_workDir), maxConcurrency: 4);
        var launcher = new JobLauncher(queue, settings);
        var updateClient = new FakeUpdateCheckClient();
        if (updateResult is not null)
        {
            updateClient.Result = updateResult;
        }

        var main = new MainViewModel(settings, store, theme, toolLocator, queue, registry, launcher, drive, updateClient, _dispatcher);
        return new Fixture(main, queue, store, drive, updateClient, theme);
    }

    private static void PlaceTool(string root, string folderName, string executableName)
    {
        var folder = Path.Combine(root, folderName);
        Directory.CreateDirectory(folder);
        File.WriteAllText(Path.Combine(folder, executableName), string.Empty);
    }

    [Fact]
    public void Pages_are_in_a_fixed_order_with_Convert_selected_first()
    {
        using var fixture = CreateMain(FakeEngine.Instant());

        Assert.Equal(
            new PageViewModel[] { fixture.Main.Convert, fixture.Main.Compress, fixture.Main.Tools, fixture.Main.Upscale, fixture.Main.Settings },
            fixture.Main.Pages);
        Assert.Same(fixture.Main.Convert, fixture.Main.SelectedPage);
    }

    [Fact]
    public void Missing_required_tools_produce_a_dependency_warning()
    {
        using var fixture = CreateMain(FakeEngine.Instant());

        Assert.True(fixture.Main.HasDependencyWarning);
        Assert.Contains("Missing required tools:", fixture.Main.DependencyWarning);
    }

    [Fact]
    public void No_dependency_warning_once_every_required_tool_is_found()
    {
        foreach (var tool in ToolManifest.Required)
        {
            PlaceTool(_workDir, tool.FolderName, tool.ExecutableNames[0]);
        }

        using var fixture = CreateMain(FakeEngine.Instant(), toolSearchRoots: new[] { _workDir });

        Assert.False(fixture.Main.HasDependencyWarning);
        Assert.Null(fixture.Main.DependencyWarning);
    }

    [Fact]
    public void Changing_the_concurrency_slider_reaches_the_live_queue()
    {
        using var fixture = CreateMain(FakeEngine.Instant());

        // The queue itself was constructed with maxConcurrency: 4, independently of
        // whatever Settings' own default (the machine's core count) happens to be — pick
        // whichever of 1/2 differs from the current setting, so the assertion below can
        // only pass via the MaxConcurrentJobsChanged -> Queue.SetMaxConcurrency wiring,
        // never by coincidentally already matching.
        var target = fixture.Main.Settings.MaxConcurrentJobs == 1 ? 2 : 1;

        fixture.Main.Settings.MaxConcurrentJobs = target;

        Assert.Equal(target, fixture.Queue.MaxConcurrency);
    }

    [Fact]
    public void Turning_on_Google_Drive_reaches_every_open_module_page()
    {
        using var fixture = CreateMain(FakeEngine.Instant());
        Assert.False(fixture.Main.Convert.IsGoogleDriveAvailable);

        fixture.Main.Settings.GoogleDriveEnabled = true;

        Assert.True(fixture.Main.Convert.IsGoogleDriveAvailable);
        Assert.True(fixture.Main.Compress.IsGoogleDriveAvailable);
        Assert.True(fixture.Main.Tools.IsGoogleDriveAvailable);
        Assert.True(fixture.Main.Upscale.IsGoogleDriveAvailable);
    }

    [Fact]
    public void OpenWithFiles_switches_to_Convert_and_stages_the_files()
    {
        using var fixture = CreateMain(FakeEngine.Instant());
        fixture.Main.SelectedPage = fixture.Main.Settings;

        fixture.Main.OpenWithFiles(new[] { @"C:\videos\clip.mp4" });

        Assert.Same(fixture.Main.Convert, fixture.Main.SelectedPage);
        Assert.Contains(@"C:\videos\clip.mp4", fixture.Main.Convert.StagedFiles);
    }

    [Fact]
    public void OpenWithFiles_with_nothing_to_open_does_not_switch_pages()
    {
        using var fixture = CreateMain(FakeEngine.Instant());
        fixture.Main.SelectedPage = fixture.Main.Settings;

        fixture.Main.OpenWithFiles(Array.Empty<string>());

        Assert.Same(fixture.Main.Settings, fixture.Main.SelectedPage);
    }

    [Fact]
    public void CheckForUpdatesOnLaunch_false_never_shows_a_banner()
    {
        using var fixture = CreateMain(
            FakeEngine.Instant(),
            checkForUpdates: false,
            updateResult: new UpdateCheckResult { HasUpdate = true, CurrentVersion = "1.0.0", LatestVersion = "2.0.0", DownloadUrl = "https://example.invalid/download" });

        Assert.False(fixture.Main.HasUpdateAvailable);
        Assert.Equal(string.Empty, fixture.Main.UpdateBannerText);
        Assert.False(fixture.Main.OpenDownloadPageCommand.CanExecute(null));
    }

    [Fact]
    public void A_real_update_shows_the_banner_until_dismissed()
    {
        using var fixture = CreateMain(
            FakeEngine.Instant(),
            checkForUpdates: true,
            updateResult: new UpdateCheckResult { HasUpdate = true, CurrentVersion = "1.0.0", LatestVersion = "1.2.0", DownloadUrl = "https://example.invalid/download" });

        Assert.True(fixture.Main.HasUpdateAvailable);
        Assert.Equal("MediaSuite 1.2.0 is available (you have 1.0.0).", fixture.Main.UpdateBannerText);
        Assert.True(fixture.Main.OpenDownloadPageCommand.CanExecute(null));

        fixture.Main.DismissUpdateBannerCommand.Execute(null);

        Assert.False(fixture.Main.HasUpdateAvailable);
    }

    [Fact]
    public void No_update_available_leaves_the_banner_hidden()
    {
        using var fixture = CreateMain(
            FakeEngine.Instant(),
            checkForUpdates: true,
            updateResult: new UpdateCheckResult { HasUpdate = false, CurrentVersion = "1.0.0" });

        Assert.False(fixture.Main.HasUpdateAvailable);
        Assert.False(fixture.Main.OpenDownloadPageCommand.CanExecute(null));
    }
}
