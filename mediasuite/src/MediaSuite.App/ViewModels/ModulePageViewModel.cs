using System.Collections.ObjectModel;
using System.Windows.Input;
using MediaSuite.App.Mvvm;
using MediaSuite.Core.Features;

namespace MediaSuite.App.ViewModels;

/// <summary>
/// Shared behaviour for the four module pages (Convert, Compress, Tools, Upscale):
/// a drop zone that stages files, and the catalogue of tools that page will offer.
/// </summary>
/// <remarks>
/// Staged files are held here rather than handed straight to a queue because the queue
/// manager itself lands in build step 3. When it does, <see cref="StagedFiles"/> becomes
/// the source for the job it enqueues; nothing else on this page has to change.
/// </remarks>
public class ModulePageViewModel : PageViewModel
{
    private string _dropPrompt = "Drop files here";
    private string _dropHint = string.Empty;

    public ModulePageViewModel(
        string title,
        string glyph,
        FeatureSection section,
        string headline,
        string description)
        : base(title, glyph)
    {
        Section = section;
        Headline = headline;
        Description = description;

        FeatureGroups = FeatureCatalog
            .GroupedBySection(section)
            .Select(group => new FeatureGroupViewModel(group.Key, group.ToList()))
            .ToList();

        StagedFiles = new ObservableCollection<string>();
        StagedFiles.CollectionChanged += (_, _) =>
        {
            OnPropertyChanged(nameof(HasStagedFiles));
            OnPropertyChanged(nameof(StagedSummary));
        };

        AddFilesCommand = new RelayCommand(parameter => AddFiles(parameter as IReadOnlyList<string>));
        ClearFilesCommand = new RelayCommand(() => StagedFiles.Clear(), () => StagedFiles.Count > 0);
    }

    public FeatureSection Section { get; }

    /// <summary>Big heading at the top of the page.</summary>
    public string Headline { get; }

    /// <summary>Sentence under the heading.</summary>
    public string Description { get; }

    /// <summary>Tools this page offers, grouped exactly as the brief groups them.</summary>
    public IReadOnlyList<FeatureGroupViewModel> FeatureGroups { get; }

    /// <summary>Files the user has dropped or browsed to, not yet queued.</summary>
    public ObservableCollection<string> StagedFiles { get; }

    public ICommand AddFilesCommand { get; }

    public ICommand ClearFilesCommand { get; }

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
