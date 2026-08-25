using System.Collections.ObjectModel;
using MediaSuite.App.Mvvm;
using MediaSuite.App.Services;
using MediaSuite.Core.Features;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;

namespace MediaSuite.App.ViewModels;

/// <summary>Owns the navigation rail and the page currently showing.</summary>
public sealed class MainViewModel : ObservableObject
{
    private PageViewModel _selectedPage;

    public MainViewModel(
        AppSettings settings,
        ISettingsStore store,
        ThemeService themeService,
        ToolLocator toolLocator)
    {
        Convert = new ModulePageViewModel(
            "Convert",
            "\uE895",
            FeatureSection.Convert,
            "Convert anything",
            "Video, audio, images including camera RAW, documents, ebooks, PDF, GIF and archives — all processed on this machine, with no size limit.")
        {
            DropPrompt = "Drop files to convert",
            DropHint = "Video, audio, images, RAW, documents, ebooks, PDF, GIF, archives.",
        };

        Compress = new ModulePageViewModel(
            "Compress",
            "\uE73F",
            FeatureSection.Compress,
            "Make files smaller",
            "Target a size or a quality level. Every compressor has Quick, Balanced and Best presets plus the raw parameters underneath.")
        {
            DropPrompt = "Drop files to compress",
            DropHint = "Video, MP3, WAV, images, PDF and GIF.",
        };

        Tools = new ModulePageViewModel(
            "Tools",
            "\uE90F",
            FeatureSection.Tools,
            "Edit and fix",
            "Crop, trim, resize, rotate and the full set of PDF page tools.")
        {
            DropPrompt = "Drop files to edit",
            DropHint = "Video, images and PDF.",
        };

        Upscale = new ModulePageViewModel(
            "Upscale",
            "\uE740",
            FeatureSection.Upscale,
            "AI photo upscaler",
            "Real-ESRGAN at 2x, 4x or 8x. Runs on the GPU through CUDA, with a CPU fallback when no supported GPU is present.")
        {
            DropPrompt = "Drop photos to upscale",
            DropHint = "JPG, PNG, WEBP, TIFF and camera RAW.",
        };

        Settings = new SettingsViewModel(settings, store, themeService, toolLocator);

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
    }

    public ModulePageViewModel Convert { get; }

    public ModulePageViewModel Compress { get; }

    public ModulePageViewModel Tools { get; }

    public ModulePageViewModel Upscale { get; }

    public SettingsViewModel Settings { get; }

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
    /// Placeholder for the queue status strip. The real queue arrives in build step 3;
    /// until then the strip explains why nothing is running.
    /// </summary>
    public string QueueStatus => "Queue idle — the job runner lands in the next build step.";
}
