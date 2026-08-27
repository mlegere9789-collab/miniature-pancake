using System.Collections.ObjectModel;
using System.IO;
using System.Windows.Input;
using MediaSuite.App.Mvvm;
using MediaSuite.Core.Features;
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
    private readonly JobLauncher _launcher;

    private string _dropPrompt = "Drop files here";
    private string _dropHint = string.Empty;
    private FeatureViewModel? _selectedFeature;
    private string _selectedOutputFormat = SameAsInput;
    private QualityPreset _selectedPreset = QualityPreset.Balanced;
    private string _outputDirectory;
    private string? _lastLaunchSummary;

    public ModulePageViewModel(
        string title,
        string glyph,
        FeatureSection section,
        string headline,
        string description,
        EngineRegistry engines,
        JobLauncher launcher,
        AppSettings settings)
        : base(title, glyph)
    {
        ArgumentNullException.ThrowIfNull(engines);

        Section = section;
        Headline = headline;
        Description = description;

        _launcher = launcher ?? throw new ArgumentNullException(nameof(launcher));
        _settings = settings ?? throw new ArgumentNullException(nameof(settings));
        _outputDirectory = settings.ResolveOutputDirectory();

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
        Presets = new[] { QualityPreset.Quick, QualityPreset.Balanced, QualityPreset.Best };

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

    public ICommand AddFilesCommand { get; }

    public ICommand ClearFilesCommand { get; }

    public ICommand BrowseOutputDirectoryCommand { get; }

    public ICommand StartCommand { get; }

    public FeatureViewModel? SelectedFeature
    {
        get => _selectedFeature;
        set
        {
            if (SetProperty(ref _selectedFeature, value))
            {
                RefreshOutputFormats();
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
        set => SetProperty(ref _selectedPreset, value);
    }

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

        var queued = _launcher.Launch(
            SelectedFeature.Descriptor,
            files,
            format,
            SelectedPreset,
            OutputDirectory);

        // Cleared on purpose: the files now live in the queue, and leaving them staged
        // invites queueing the same batch twice.
        StagedFiles.Clear();

        LastLaunchSummary = queued.Count == 1
            ? $"Queued 1 job — {SelectedFeature.Name}."
            : $"Queued {queued.Count} jobs — {SelectedFeature.Name}.";
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
