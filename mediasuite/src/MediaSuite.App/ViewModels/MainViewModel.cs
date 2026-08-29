using System.Collections.ObjectModel;
using System.Diagnostics;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using System.Windows.Threading;
using MediaSuite.App.Mvvm;
using MediaSuite.App.Services;
using MediaSuite.Core.Features;
using MediaSuite.Core.GoogleDrive;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using MediaSuite.Core.Updates;

namespace MediaSuite.App.ViewModels;

/// <summary>Owns the navigation rail and the page currently showing.</summary>
public sealed class MainViewModel : ObservableObject, IDisposable
{
    private readonly IUpdateCheckClient _updateChecker;

    private PageViewModel _selectedPage;
    private bool _disposed;
    private UpdateCheckResult? _updateCheck;
    private bool _updateBannerDismissed;

    public MainViewModel(
        AppSettings settings,
        ISettingsStore store,
        ThemeService themeService,
        ToolLocator toolLocator,
        JobQueueManager queue,
        EngineRegistry engines,
        JobLauncher launcher,
        IGoogleDriveClient driveClient,
        IUpdateCheckClient updateChecker,
        Dispatcher dispatcher)
    {
        _updateChecker = updateChecker ?? throw new ArgumentNullException(nameof(updateChecker));
        Convert = new ModulePageViewModel(
            "Convert",
            "\uE895",
            FeatureSection.Convert,
            "Convert anything",
            "Video, audio, images including camera RAW, documents, ebooks, PDF, GIF and archives — all processed on this machine, with no size limit.",
            engines,
            launcher,
            settings,
            store,
            driveClient)
        {
            DropPrompt = "Drop files to convert",
            DropHint = "Video, audio, images, RAW, documents, ebooks, PDF, GIF, archives.",
        };

        Compress = new ModulePageViewModel(
            "Compress",
            "\uE73F",
            FeatureSection.Compress,
            "Make files smaller",
            "Target a size or a quality level. Every compressor has Quick, Balanced and Best presets plus the raw parameters underneath.",
            engines,
            launcher,
            settings,
            store,
            driveClient)
        {
            DropPrompt = "Drop files to compress",
            DropHint = "Video, MP3, WAV, images, PDF and GIF.",
        };

        Tools = new ModulePageViewModel(
            "Tools",
            "\uE90F",
            FeatureSection.Tools,
            "Edit and fix",
            "Crop, trim, resize, rotate and the full set of PDF page tools.",
            engines,
            launcher,
            settings,
            store,
            driveClient)
        {
            DropPrompt = "Drop files to edit",
            DropHint = "Video, images and PDF.",
        };

        Upscale = new ModulePageViewModel(
            "Upscale",
            "\uE740",
            FeatureSection.Upscale,
            "AI photo upscaler",
            "Real-ESRGAN at 2x, 4x or 8x. Runs on the GPU through CUDA, with a CPU fallback when no supported GPU is present.",
            engines,
            launcher,
            settings,
            store,
            driveClient)
        {
            DropPrompt = "Drop photos to upscale",
            DropHint = "JPG, PNG, WEBP, TIFF and camera RAW.",
        };

        Settings = new SettingsViewModel(settings, store, themeService, toolLocator, driveClient);
        Queue = new JobQueueViewModel(queue, dispatcher);

        // The concurrency slider has to reach a queue that is already running, not just
        // the settings file.
        Settings.MaxConcurrentJobsChanged += (_, value) => Queue.SetMaxConcurrency(value);

        // Flipping the Drive master switch has to reach every module page already open,
        // not just take effect on the next launch.
        Settings.GoogleDriveEnabledChanged += (_, enabled) =>
        {
            Convert.IsGoogleDriveAvailable = enabled;
            Compress.IsGoogleDriveAvailable = enabled;
            Tools.IsGoogleDriveAvailable = enabled;
            Upscale.IsGoogleDriveAvailable = enabled;
        };

        Pages = new ObservableCollection<PageViewModel>
        {
            Convert,
            Compress,
            Tools,
            Upscale,
            Settings,
        };

        _selectedPage = Convert;

        var missing = toolLocator.MissingRequiredTools();
        DependencyWarning = missing.Count == 0
            ? null
            : $"Missing required tools: {string.Join(", ", missing.Select(t => t.DisplayName))}. "
              + "Conversions that need them will stay disabled — see Settings for where to put them.";

        OpenDownloadPageCommand = new RelayCommand(OpenDownloadPage, () => UpdateDownloadUrl is not null);
        DismissUpdateBannerCommand = new RelayCommand(DismissUpdateBanner);

        if (settings.CheckForUpdatesOnLaunch)
        {
            _ = CheckForUpdateAsync();
        }
    }

    public ModulePageViewModel Convert { get; }

    public ModulePageViewModel Compress { get; }

    public ModulePageViewModel Tools { get; }

    public ModulePageViewModel Upscale { get; }

    public SettingsViewModel Settings { get; }

    /// <summary>Live queue: rows, counts, pause and cancel.</summary>
    public JobQueueViewModel Queue { get; }

    public ObservableCollection<PageViewModel> Pages { get; }

    public PageViewModel SelectedPage
    {
        get => _selectedPage;
        set => SetProperty(ref _selectedPage, value);
    }

    /// <summary>Banner text when a required binary is not installed; null when all is well.</summary>
    public string? DependencyWarning { get; }

    public bool HasDependencyWarning => DependencyWarning is not null;

    /// <summary>
    /// True once a check finds a real, newer release and the user has not dismissed the
    /// banner for it. The app never updates itself — this only ever offers the download
    /// page, and the user decides.
    /// </summary>
    public bool HasUpdateAvailable => _updateCheck?.HasUpdate == true && !_updateBannerDismissed;

    public string UpdateBannerText => _updateCheck is null
        ? string.Empty
        : $"MediaSuite {_updateCheck.LatestVersion} is available (you have {_updateCheck.CurrentVersion}).";

    private string? UpdateDownloadUrl => _updateCheck?.DownloadUrl;

    public ICommand OpenDownloadPageCommand { get; }

    public ICommand DismissUpdateBannerCommand { get; }

    private async Task CheckForUpdateAsync()
    {
        // Deliberately no ConfigureAwait(false): this needs to resume on the UI thread
        // to raise PropertyChanged safely, and WPF's SynchronizationContext (captured
        // here since the constructor runs on the UI thread) already provides that.
        _updateCheck = await _updateChecker.CheckAsync(CancellationToken.None);

        OnPropertyChanged(nameof(HasUpdateAvailable));
        OnPropertyChanged(nameof(UpdateBannerText));
        CommandManager.InvalidateRequerySuggested();
    }

    private void OpenDownloadPage()
    {
        if (UpdateDownloadUrl is null)
        {
            return;
        }

        Process.Start(new ProcessStartInfo(UpdateDownloadUrl) { UseShellExecute = true });
    }

    private void DismissUpdateBanner()
    {
        _updateBannerDismissed = true;
        OnPropertyChanged(nameof(HasUpdateAvailable));
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Queue.Dispose();
    }
}
