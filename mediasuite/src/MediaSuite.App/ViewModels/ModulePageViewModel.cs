using System.Collections.ObjectModel;
using System.IO;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Input;
using MediaSuite.App.Mvvm;
using MediaSuite.Core.Features;
using MediaSuite.Core.GoogleDrive;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using Microsoft.Win32;

namespace MediaSuite.App.ViewModels;

/// <summary>
/// A module page: drop files, pick a tool and a format, and start the work.
/// </summary>
public class ModulePageViewModel : PageViewModel
{
    /// <summary>Shown in the format list for tools that keep whatever format came in.</summary>
    public const string SameAsInput = "Same as input";

    private readonly AppSettings _settings;
    private readonly ISettingsStore _store;
    private readonly JobLauncher _launcher;
    private readonly IGoogleDriveClient _driveClient;

    private string _dropPrompt = "Drop files here";
    private string _dropHint = string.Empty;
    private FeatureViewModel? _selectedFeature;
    private string _selectedOutputFormat = SameAsInput;
    private QualityPreset _selectedPreset = QualityPreset.Balanced;
    private string _outputDirectory;
    private string? _lastLaunchSummary;
    private CustomPreset? _selectedSavedPreset;
    private string _advancedOptionsText = string.Empty;
    private string _newPresetName = string.Empty;
    private string? _presetFeedback;
    private bool _isGoogleDriveAvailable;
    private bool _uploadToGoogleDrive;
    private GoogleDriveFolder? _selectedDriveFolder;
    private string _newDriveFolderName = string.Empty;
    private string? _driveFolderHint;

    public ModulePageViewModel(
        string title,
        string glyph,
        FeatureSection section,
        string headline,
        string description,
        EngineRegistry engines,
        JobLauncher launcher,
        AppSettings settings,
        ISettingsStore store,
        IGoogleDriveClient driveClient)
        : base(title, glyph)
    {
        ArgumentNullException.ThrowIfNull(engines);

        Section = section;
        Headline = headline;
        Description = description;

        _launcher = launcher ?? throw new ArgumentNullException(nameof(launcher));
        _settings = settings ?? throw new ArgumentNullException(nameof(settings));
        _store = store ?? throw new ArgumentNullException(nameof(store));
        _driveClient = driveClient ?? throw new ArgumentNullException(nameof(driveClient));
        _outputDirectory = settings.ResolveOutputDirectory();
        _isGoogleDriveAvailable = settings.GoogleDriveEnabled;

        FeatureGroups = FeatureCatalog
            .GroupedBySection(section)
            .Select(group => new FeatureGroupViewModel(
                group.Key,
                group.Select(feature => new FeatureViewModel(feature, engines.SupportsOperation(feature.OperationId)))
                    .ToList()))
            .ToList();

        ReadyFeatures = FeatureGroups
            .SelectMany(group => group.Features)
            .Where(feature => feature.IsAvailable)
            .ToList();

        OutputFormats = new ObservableCollection<string>();
        Presets = new[] { QualityPreset.Quick, QualityPreset.Balanced, QualityPreset.Best, QualityPreset.Custom };
        SavedPresets = new ObservableCollection<CustomPreset>();
        DriveFolders = new ObservableCollection<GoogleDriveFolder>();

        StagedFiles = new ObservableCollection<string>();
        StagedFiles.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(HasStagedFiles));
            OnPropertyChanged(nameof(StagedSummary));
            OnPropertyChanged(nameof(CanStart));
            CommandManager.InvalidateRequerySuggested();
        };

        AddFilesCommand = new RelayCommand(parameter => AddFiles(parameter as IReadOnlyList<string>));
        ClearFilesCommand = new RelayCommand(() => StagedFiles.Clear(), () => StagedFiles.Count > 0);
        BrowseOutputDirectoryCommand = new RelayCommand(BrowseOutputDirectory);
        StartCommand = new RelayCommand(Start, () => CanStart);
        SavePresetCommand = new RelayCommand(SavePreset, () => SelectedFeature is not null && NewPresetName.Trim().Length > 0);
        DeletePresetCommand = new RelayCommand(DeletePreset, () => SelectedSavedPreset is not null);
        CreateDriveFolderCommand = new RelayCommand(
            async () => await CreateDriveFolderAsync(), () => NewDriveFolderName.Trim().Length > 0);

        SelectedFeature = ReadyFeatures.FirstOrDefault();
    }

    public FeatureSection Section { get; }

    public string Headline { get; }

    public string Description { get; }

    public IReadOnlyList<FeatureGroupViewModel> FeatureGroups { get; }

    /// <summary>Tools on this page that an engine can actually run today.</summary>
    public IReadOnlyList<FeatureViewModel> ReadyFeatures { get; }

    public bool HasReadyFeatures => ReadyFeatures.Count > 0;

    public ObservableCollection<string> StagedFiles { get; }

    public ObservableCollection<string> OutputFormats { get; }

    public IReadOnlyList<QualityPreset> Presets { get; }

    /// <summary>Named option sets the user saved earlier for whichever tool is selected.</summary>
    public ObservableCollection<CustomPreset> SavedPresets { get; }

    public ICommand AddFilesCommand { get; }

    public ICommand ClearFilesCommand { get; }

    public ICommand BrowseOutputDirectoryCommand { get; }

    public ICommand StartCommand { get; }

    public ICommand SavePresetCommand { get; }

    public ICommand DeletePresetCommand { get; }

    public FeatureViewModel? SelectedFeature
    {
        get => _selectedFeature;
        set
        {
            if (SetProperty(ref _selectedFeature, value))
            {
                RefreshOutputFormats();
                RefreshSavedPresets();

                // A previous tool's advanced options are meaningless for a different one.
                _selectedSavedPreset = null;
                AdvancedOptionsText = string.Empty;
                NewPresetName = string.Empty;
                OnPropertyChanged(nameof(SelectedSavedPreset));

                OnPropertyChanged(nameof(CanStart));
                OnPropertyChanged(nameof(StartHint));
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public string SelectedOutputFormat
    {
        get => _selectedOutputFormat;
        set => SetProperty(ref _selectedOutputFormat, value);
    }

    public QualityPreset SelectedPreset
    {
        get => _selectedPreset;
        set
        {
            if (SetProperty(ref _selectedPreset, value))
            {
                OnPropertyChanged(nameof(IsCustomPreset));
            }
        }
    }

    public bool IsCustomPreset => SelectedPreset == QualityPreset.Custom;

    /// <summary>"key=value" lines edited by hand, used as the job's advanced options when the Custom preset is selected.</summary>
    public string AdvancedOptionsText
    {
        get => _advancedOptionsText;
        set => SetProperty(ref _advancedOptionsText, value);
    }

    /// <summary>Name typed in before pressing "Save as preset".</summary>
    public string NewPresetName
    {
        get => _newPresetName;
        set
        {
            if (SetProperty(ref _newPresetName, value))
            {
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    public CustomPreset? SelectedSavedPreset
    {
        get => _selectedSavedPreset;
        set
        {
            if (SetProperty(ref _selectedSavedPreset, value))
            {
                if (value is not null)
                {
                    AdvancedOptionsText = OptionsTextFormat.Format(value.Options);
                    NewPresetName = value.Name;
                }

                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    /// <summary>Feedback from the last preset save/delete; null before either happens.</summary>
    public string? PresetFeedback
    {
        get => _presetFeedback;
        private set
        {
            if (SetProperty(ref _presetFeedback, value))
            {
                OnPropertyChanged(nameof(HasPresetFeedback));
            }
        }
    }

    public bool HasPresetFeedback => !string.IsNullOrEmpty(PresetFeedback);

    /// <summary>Mirrors Settings' master switch, so this page's checkbox only shows up when Drive is turned on.</summary>
    public bool IsGoogleDriveAvailable
    {
        get => _isGoogleDriveAvailable;
        set
        {
            if (SetProperty(ref _isGoogleDriveAvailable, value) && !value)
            {
                UploadToGoogleDrive = false;
            }
        }
    }

    public bool UploadToGoogleDrive
    {
        get => _uploadToGoogleDrive;
        set
        {
            if (SetProperty(ref _uploadToGoogleDrive, value) && value && DriveFolders.Count == 0)
            {
                _ = RefreshDriveFoldersAsync();
            }
        }
    }

    public ObservableCollection<GoogleDriveFolder> DriveFolders { get; }

    public GoogleDriveFolder? SelectedDriveFolder
    {
        get => _selectedDriveFolder;
        set => SetProperty(ref _selectedDriveFolder, value);
    }

    /// <summary>Name typed in before pressing "New folder".</summary>
    public string NewDriveFolderName
    {
        get => _newDriveFolderName;
        set
        {
            if (SetProperty(ref _newDriveFolderName, value))
            {
                CommandManager.InvalidateRequerySuggested();
            }
        }
    }

    /// <summary>Status line under the folder picker — loading, empty, an error, or null once folders are showing.</summary>
    public string? DriveFolderHint
    {
        get => _driveFolderHint;
        private set
        {
            if (SetProperty(ref _driveFolderHint, value))
            {
                OnPropertyChanged(nameof(HasDriveFolderHint));
            }
        }
    }

    public bool HasDriveFolderHint => !string.IsNullOrEmpty(DriveFolderHint);

    public ICommand CreateDriveFolderCommand { get; }

    public string OutputDirectory
    {
        get => _outputDirectory;
        set => SetProperty(ref _outputDirectory, value);
    }

    /// <summary>What happened the last time Start was pressed; null before then.</summary>
    public string? LastLaunchSummary
    {
        get => _lastLaunchSummary;
        private set
        {
            if (SetProperty(ref _lastLaunchSummary, value))
            {
                OnPropertyChanged(nameof(HasLaunchSummary));
            }
        }
    }

    public bool HasLaunchSummary => !string.IsNullOrEmpty(LastLaunchSummary);

    public string DropPrompt
    {
        get => _dropPrompt;
        set => SetProperty(ref _dropPrompt, value);
    }

    public string DropHint
    {
        get => _dropHint;
        set => SetProperty(ref _dropHint, value);
    }

    public bool HasStagedFiles => StagedFiles.Count > 0;

    public bool CanStart => HasStagedFiles && SelectedFeature is not null;

    public string StartHint => SelectedFeature is null
        ? "The tools on this page arrive in a later build step."
        : $"Runs {SelectedFeature.Name} on every staged file, one job each.";

    /// <summary>"12 files · 1.4 GB", shown above the staged list.</summary>
    public string StagedSummary
    {
        get
        {
            if (StagedFiles.Count == 0)
            {
                return "No files staged";
            }

            var label = StagedFiles.Count == 1 ? "1 file" : $"{StagedFiles.Count} files";
            var totalBytes = 0L;

            foreach (var path in StagedFiles)
            {
                try
                {
                    var info = new FileInfo(path);
                    if (info.Exists)
                    {
                        totalBytes += info.Length;
                    }
                }
                catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
                {
                    // A file that vanished between the drop and now just does not count.
                }
            }

            return $"{label} · {FormatSize(totalBytes)}";
        }
    }

    private void Start()
    {
        if (SelectedFeature is null || StagedFiles.Count == 0)
        {
            return;
        }

        var format = string.Equals(SelectedOutputFormat, SameAsInput, StringComparison.Ordinal)
            ? null
            : SelectedOutputFormat;

        var files = StagedFiles.ToList();
        var options = IsCustomPreset ? OptionsTextFormat.Parse(AdvancedOptionsText) : null;
        var uploadToGoogleDrive = IsGoogleDriveAvailable && UploadToGoogleDrive;
        var driveFolderId = SelectedDriveFolder?.Id;

        if (uploadToGoogleDrive && !string.Equals(_settings.LastGoogleDriveFolderId, driveFolderId, StringComparison.Ordinal))
        {
            _settings.LastGoogleDriveFolderId = driveFolderId;
            _store.Save(_settings);
        }

        var queued = _launcher.Launch(
            SelectedFeature.Descriptor,
            files,
            format,
            SelectedPreset,
            OutputDirectory,
            options,
            uploadToGoogleDrive,
            driveFolderId);

        // Cleared on purpose: the files now live in the queue, and leaving them staged
        // invites queueing the same batch twice.
        StagedFiles.Clear();

        LastLaunchSummary = queued.Count == 1
            ? $"Queued 1 job — {SelectedFeature.Name}."
            : $"Queued {queued.Count} jobs — {SelectedFeature.Name}.";
    }

    private void SavePreset()
    {
        if (SelectedFeature is null)
        {
            return;
        }

        var name = NewPresetName.Trim();
        if (name.Length == 0)
        {
            return;
        }

        var preset = new CustomPreset
        {
            Name = name,
            Options = OptionsTextFormat.Parse(AdvancedOptionsText),
        };

        _settings.SaveCustomPreset(SelectedFeature.OperationId, preset);
        _store.Save(_settings);
        RefreshSavedPresets();
        SelectedSavedPreset = SavedPresets.FirstOrDefault(p => string.Equals(p.Name, name, StringComparison.OrdinalIgnoreCase));

        PresetFeedback = $"Saved preset \"{name}\".";
    }

    private void DeletePreset()
    {
        if (SelectedFeature is null || SelectedSavedPreset is null)
        {
            return;
        }

        var name = SelectedSavedPreset.Name;
        _settings.DeleteCustomPreset(SelectedFeature.OperationId, name);
        _store.Save(_settings);
        SelectedSavedPreset = null;
        AdvancedOptionsText = string.Empty;
        NewPresetName = string.Empty;
        RefreshSavedPresets();

        PresetFeedback = $"Deleted preset \"{name}\".";
    }

    private void RefreshSavedPresets()
    {
        SavedPresets.Clear();

        if (SelectedFeature is null)
        {
            return;
        }

        foreach (var preset in _settings.PresetsFor(SelectedFeature.OperationId))
        {
            SavedPresets.Add(preset);
        }
    }

    private async Task RefreshDriveFoldersAsync()
    {
        try
        {
            DriveFolderHint = "Loading folders…";
            var folders = await _driveClient.ListFoldersAsync(null, CancellationToken.None);

            DriveFolders.Clear();
            foreach (var folder in folders)
            {
                DriveFolders.Add(folder);
            }

            SelectedDriveFolder = DriveFolders.FirstOrDefault(
                folder => string.Equals(folder.Id, _settings.LastGoogleDriveFolderId, StringComparison.Ordinal));

            DriveFolderHint = DriveFolders.Count == 0 ? "No folders yet — Drive root will be used." : null;
        }
        catch (GoogleDriveNotSignedInException)
        {
            DriveFolderHint = "Sign in to Google Drive from Settings to pick a folder.";
        }
        catch (Exception ex)
        {
            DriveFolderHint = $"Could not load Drive folders: {ex.Message}";
        }
    }

    private async Task CreateDriveFolderAsync()
    {
        var name = NewDriveFolderName.Trim();
        if (name.Length == 0)
        {
            return;
        }

        try
        {
            var id = await _driveClient.CreateFolderAsync(name, null, CancellationToken.None);
            var folder = new GoogleDriveFolder { Id = id, Name = name };

            DriveFolders.Add(folder);
            SelectedDriveFolder = folder;
            NewDriveFolderName = string.Empty;
            DriveFolderHint = null;
        }
        catch (Exception ex)
        {
            DriveFolderHint = $"Could not create the folder: {ex.Message}";
        }
    }

    private void RefreshOutputFormats()
    {
        OutputFormats.Clear();

        if (SelectedFeature is null)
        {
            SelectedOutputFormat = SameAsInput;
            return;
        }

        var operationId = SelectedFeature.OperationId;
        var forced = OutputFormatRules.ForcedFormat(operationId);

        if (forced is not null)
        {
            // The tool's whole purpose is that format; offering a choice would be a lie.
            OutputFormats.Add(forced);
            SelectedOutputFormat = forced;
            return;
        }

        if (OutputFormatRules.KeepsSourceFormat(operationId))
        {
            OutputFormats.Add(SameAsInput);
        }

        foreach (var format in OperationFamily.OutputFormatsFor(operationId))
        {
            OutputFormats.Add(format.Extension);
        }

        SelectedOutputFormat = OutputFormats.FirstOrDefault() ?? SameAsInput;
    }

    private void BrowseOutputDirectory()
    {
        var dialog = new OpenFolderDialog { Title = "Choose where the results go", Multiselect = false };

        if (Directory.Exists(OutputDirectory))
        {
            dialog.InitialDirectory = OutputDirectory;
        }

        if (dialog.ShowDialog() == true)
        {
            OutputDirectory = dialog.FolderName;
        }
    }

    private void AddFiles(IReadOnlyList<string>? files)
    {
        if (files is null)
        {
            return;
        }

        var existing = new HashSet<string>(StagedFiles, StringComparer.OrdinalIgnoreCase);

        foreach (var file in files)
        {
            if (existing.Add(file))
            {
                StagedFiles.Add(file);
            }
        }
    }

    private static string FormatSize(long bytes)
    {
        string[] units = { "B", "KB", "MB", "GB", "TB" };
        double value = bytes;
        var unit = 0;

        while (value >= 1024 && unit < units.Length - 1)
        {
            value /= 1024;
            unit++;
        }

        return unit == 0 ? $"{bytes} {units[unit]}" : $"{value:0.#} {units[unit]}";
    }
}
