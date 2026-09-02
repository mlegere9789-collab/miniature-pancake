using System.Collections.ObjectModel;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using MediaSuite.App.Mvvm;
using MediaSuite.App.Services;
using MediaSuite.Core.GoogleDrive;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using Microsoft.Win32;

namespace MediaSuite.App.ViewModels;

/// <summary>
/// The Settings screen. Every change is applied immediately and written straight to
/// disk — there is no OK/Cancel, so the app never loses a preference to a crash.
/// </summary>
public sealed class SettingsViewModel : PageViewModel
{
    private readonly AppSettings _settings;
    private readonly ISettingsStore _store;
    private readonly ThemeService _themeService;
    private readonly ToolLocator _toolLocator;
    private readonly IGoogleDriveClient _driveClient;

    private bool _isSignedInToGoogleDrive;
    private string _googleDriveStatus = "Checking\u2026";

    public SettingsViewModel(
        AppSettings settings,
        ISettingsStore store,
        ThemeService themeService,
        ToolLocator toolLocator,
        IGoogleDriveClient driveClient)
        : base("Settings", "\uE713")
    {
        _settings = settings;
        _store = store;
        _themeService = themeService;
        _toolLocator = toolLocator;
        _driveClient = driveClient;

        Tools = new ObservableCollection<ToolStatusViewModel>();

        BrowseOutputDirectoryCommand = new RelayCommand(BrowseOutputDirectory);
        BrowseTempDirectoryCommand = new RelayCommand(BrowseTempDirectory);
        BrowseToolsDirectoryCommand = new RelayCommand(BrowseToolsDirectory);
        RefreshToolsCommand = new RelayCommand(RefreshTools);
        BrowseGoogleDriveCredentialsCommand = new RelayCommand(BrowseGoogleDriveCredentials);
        SignInToGoogleDriveCommand = new RelayCommand(
            async () => await SignInToGoogleDriveAsync(), () => !_isSignedInToGoogleDrive);
        SignOutOfGoogleDriveCommand = new RelayCommand(
            async () => await SignOutOfGoogleDriveAsync(), () => _isSignedInToGoogleDrive);

        RefreshTools();
        _ = RefreshGoogleDriveStatusAsync();
    }

    /// <summary>
    /// Raised when the concurrency slider moves, so a queue that is already running can
    /// widen or narrow without a restart.
    /// </summary>
    public event EventHandler<int>? MaxConcurrentJobsChanged;

    /// <summary>
    /// Raised when the Google Drive master switch is flipped, so a module page already
    /// open can show or hide its own upload checkbox without a restart.
    /// </summary>
    public event EventHandler<bool>? GoogleDriveEnabledChanged;

    // --- Appearance -------------------------------------------------------

    public bool IsThemeSystem
    {
        get => _settings.Theme == ThemeMode.System;
        set => SetTheme(value, ThemeMode.System);
    }

    public bool IsThemeLight
    {
        get => _settings.Theme == ThemeMode.Light;
        set => SetTheme(value, ThemeMode.Light);
    }

    public bool IsThemeDark
    {
        get => _settings.Theme == ThemeMode.Dark;
        set => SetTheme(value, ThemeMode.Dark);
    }

    // --- Output -----------------------------------------------------------

    public string OutputDirectory
    {
        get => _settings.ResolveOutputDirectory();
        set
        {
            if (string.Equals(_settings.DefaultOutputDirectory, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.DefaultOutputDirectory = string.IsNullOrWhiteSpace(value) ? null : value;
            Persist();
            OnPropertyChanged();
        }
    }

    public bool PreserveFolderStructure
    {
        get => _settings.PreserveFolderStructure;
        set
        {
            if (_settings.PreserveFolderStructure == value)
            {
                return;
            }

            _settings.PreserveFolderStructure = value;
            Persist();
            OnPropertyChanged();
        }
    }

    public ICommand BrowseOutputDirectoryCommand { get; }

    // --- Performance ------------------------------------------------------

    public int MaxConcurrentJobs
    {
        get => _settings.MaxConcurrentJobs;
        set
        {
            var clamped = Math.Clamp(value, 1, MaxConcurrencyLimit);
            if (_settings.MaxConcurrentJobs == clamped)
            {
                return;
            }

            _settings.MaxConcurrentJobs = clamped;
            Persist();
            MaxConcurrentJobsChanged?.Invoke(this, clamped);
            OnPropertyChanged();
            OnPropertyChanged(nameof(ConcurrencyHint));
        }
    }

    public int MaxConcurrencyLimit => AppSettings.MaxConcurrencyLimit;

    public int CoreCount => Environment.ProcessorCount;

    public string ConcurrencyHint =>
        MaxConcurrentJobs == CoreCount
            ? $"{MaxConcurrentJobs} at once (matches this machine's {CoreCount} cores)"
            : $"{MaxConcurrentJobs} at once (this machine has {CoreCount} cores)";

    public bool UseCustomTempDirectory
    {
        get => _settings.TempStorage == TempStorageMode.CustomFolder;
        set
        {
            var mode = value ? TempStorageMode.CustomFolder : TempStorageMode.Disk;
            if (_settings.TempStorage == mode)
            {
                return;
            }

            _settings.TempStorage = mode;
            Persist();
            OnPropertyChanged();
            OnPropertyChanged(nameof(TempDirectory));
        }
    }

    public string TempDirectory
    {
        get => _settings.ResolveTempDirectory();
        set
        {
            if (string.Equals(_settings.CustomTempDirectory, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.CustomTempDirectory = string.IsNullOrWhiteSpace(value) ? null : value;
            Persist();
            OnPropertyChanged();
        }
    }

    public ICommand BrowseTempDirectoryCommand { get; }

    // --- Updates and integrations ----------------------------------------

    public bool CheckForUpdatesOnLaunch
    {
        get => _settings.CheckForUpdatesOnLaunch;
        set
        {
            if (_settings.CheckForUpdatesOnLaunch == value)
            {
                return;
            }

            _settings.CheckForUpdatesOnLaunch = value;
            Persist();
            OnPropertyChanged();
        }
    }

    public bool GoogleDriveEnabled
    {
        get => _settings.GoogleDriveEnabled;
        set
        {
            if (_settings.GoogleDriveEnabled == value)
            {
                return;
            }

            _settings.GoogleDriveEnabled = value;
            Persist();
            OnPropertyChanged();
            GoogleDriveEnabledChanged?.Invoke(this, value);
        }
    }

    public string GoogleDriveCredentialsPath
    {
        get => _settings.GoogleDriveCredentialsPath ?? "google-drive-credentials.json in the settings folder";
        set
        {
            if (string.Equals(_settings.GoogleDriveCredentialsPath, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.GoogleDriveCredentialsPath = string.IsNullOrWhiteSpace(value) ? null : value;
            Persist();
            OnPropertyChanged();
        }
    }

    public ICommand BrowseGoogleDriveCredentialsCommand { get; }

    public bool IsSignedInToGoogleDrive
    {
        get => _isSignedInToGoogleDrive;
        private set
        {
            if (SetProperty(ref _isSignedInToGoogleDrive, value))
            {
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public string GoogleDriveStatus
    {
        get => _googleDriveStatus;
        private set => SetProperty(ref _googleDriveStatus, value);
    }

    public ICommand SignInToGoogleDriveCommand { get; }

    public ICommand SignOutOfGoogleDriveCommand { get; }

    // --- Bundled tools ----------------------------------------------------

    public string ToolsDirectory
    {
        get => _settings.ToolsDirectory ?? "tools\\ next to MediaSuite.exe";
        set
        {
            if (string.Equals(_settings.ToolsDirectory, value, StringComparison.Ordinal))
            {
                return;
            }

            _settings.ToolsDirectory = string.IsNullOrWhiteSpace(value) ? null : value;
            Persist();
            OnPropertyChanged();
            RefreshTools();
        }
    }

    public ICommand BrowseToolsDirectoryCommand { get; }

    public ICommand RefreshToolsCommand { get; }

    public ObservableCollection<ToolStatusViewModel> Tools { get; }

    public string ToolSummary
    {
        get
        {
            var found = Tools.Count(t => t.IsFound);
            var missingRequired = Tools.Count(t => t.IsRequired && !t.IsFound);

            return missingRequired > 0
                ? $"{found} of {Tools.Count} tools found — {missingRequired} required tool(s) still missing."
                : $"{found} of {Tools.Count} tools found.";
        }
    }

    private void RefreshTools()
    {
        _toolLocator.Refresh();
        Tools.Clear();

        foreach (var descriptor in ToolManifest.All)
        {
            Tools.Add(new ToolStatusViewModel(descriptor, _toolLocator.Locate(descriptor.Id)));
        }

        OnPropertyChanged(nameof(ToolSummary));
    }

    private void SetTheme(bool isChecked, ThemeMode mode)
    {
        if (!isChecked || _settings.Theme == mode)
        {
            return;
        }

        _settings.Theme = mode;
        _themeService.Apply(mode);
        Persist();

        OnPropertyChanged(nameof(IsThemeSystem));
        OnPropertyChanged(nameof(IsThemeLight));
        OnPropertyChanged(nameof(IsThemeDark));
    }

    private void BrowseOutputDirectory()
    {
        var chosen = PickFolder("Choose the default save folder", OutputDirectory);
        if (chosen is not null)
        {
            OutputDirectory = chosen;
        }
    }

    private void BrowseTempDirectory()
    {
        var chosen = PickFolder("Choose a working folder for temporary files", TempDirectory);
        if (chosen is null)
        {
            return;
        }

        UseCustomTempDirectory = true;
        TempDirectory = chosen;
    }

    private void BrowseToolsDirectory()
    {
        var chosen = PickFolder("Choose the folder holding the bundled tools", _settings.ToolsDirectory);
        if (chosen is not null)
        {
            ToolsDirectory = chosen;
        }
    }

    private void BrowseGoogleDriveCredentials()
    {
        var dialog = new OpenFileDialog
        {
            Title = "Choose the Google Drive OAuth client file",
            Filter = "JSON files (*.json)|*.json|All files (*.*)|*.*",
        };

        if (dialog.ShowDialog() == true)
        {
            GoogleDriveCredentialsPath = dialog.FileName;
        }
    }

    private async Task SignInToGoogleDriveAsync()
    {
        GoogleDriveStatus = "Signing in — check your browser…";

        try
        {
            await _driveClient.SignInAsync(CancellationToken.None);
            IsSignedInToGoogleDrive = true;
            GoogleDriveStatus = "Signed in.";
        }
        catch (Exception ex)
        {
            // A failed sign-in (missing credentials file, closed consent screen, no
            // network) is common and recoverable — show it, don't crash the screen.
            IsSignedInToGoogleDrive = false;
            GoogleDriveStatus = $"Sign-in failed: {ex.Message}";
        }
    }

    private async Task SignOutOfGoogleDriveAsync()
    {
        await _driveClient.SignOutAsync();
        IsSignedInToGoogleDrive = false;
        GoogleDriveStatus = "Not signed in.";
    }

    private async Task RefreshGoogleDriveStatusAsync()
    {
        try
        {
            IsSignedInToGoogleDrive = await _driveClient.IsSignedInAsync(CancellationToken.None);
            GoogleDriveStatus = IsSignedInToGoogleDrive ? "Signed in." : "Not signed in.";
        }
        catch (Exception ex)
        {
            GoogleDriveStatus = $"Could not check Google Drive status: {ex.Message}";
        }
    }

    private static string? PickFolder(string title, string? initialDirectory)
    {
        var dialog = new OpenFolderDialog
        {
            Title = title,
            Multiselect = false,
        };

        if (!string.IsNullOrWhiteSpace(initialDirectory) && Directory.Exists(initialDirectory))
        {
            dialog.InitialDirectory = initialDirectory;
        }

        return dialog.ShowDialog() == true ? dialog.FolderName : null;
    }

    private void Persist() => _store.Save(_settings);
}
