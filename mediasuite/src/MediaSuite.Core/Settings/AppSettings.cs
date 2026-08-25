namespace MediaSuite.Core.Settings;

/// <summary>
/// Everything the app remembers between launches. Mutable and JSON-serialisable on
/// purpose: the Settings screen edits an instance directly and hands it back to the store.
/// </summary>
public sealed class AppSettings
{
    /// <summary>Bumped when a future version needs to migrate an old file.</summary>
    public int SchemaVersion { get; set; } = 1;

    public ThemeMode Theme { get; set; } = ThemeMode.System;

    /// <summary>Default save folder. Null means <see cref="AppPaths.DefaultOutputDirectory"/>.</summary>
    public string? DefaultOutputDirectory { get; set; }

    /// <summary>Recreate the source folder tree for batches taken from nested folders.</summary>
    public bool PreserveFolderStructure { get; set; }

    /// <summary>
    /// How many jobs run at once. Defaults to the CPU core count, overridable from
    /// Settings; clamped on load so a hand-edited file cannot wedge the queue.
    /// </summary>
    public int MaxConcurrentJobs { get; set; } = Environment.ProcessorCount;

    public TempStorageMode TempStorage { get; set; } = TempStorageMode.Disk;

    /// <summary>Used only when <see cref="TempStorage"/> is <see cref="TempStorageMode.CustomFolder"/>.</summary>
    public string? CustomTempDirectory { get; set; }

    /// <summary>Check the version manifest at launch and offer the download page. Never silent.</summary>
    public bool CheckForUpdatesOnLaunch { get; set; } = true;

    /// <summary>Master switch for the optional Google Drive output. Off until the user signs in.</summary>
    public bool GoogleDriveEnabled { get; set; }

    /// <summary>Remember the Drive folder chosen last time, so repeat uploads are one click.</summary>
    public string? LastGoogleDriveFolderId { get; set; }

    /// <summary>Optional override for the bundled-tools folder, for a portable install.</summary>
    public string? ToolsDirectory { get; set; }

    private Dictionary<string, string>? _toolPathOverrides;

    /// <summary>
    /// Per-tool binary overrides, keyed by <c>ExternalToolId</c> name. Backed by a field
    /// because a hand-edited or older settings file can deserialise this as null.
    /// </summary>
    public Dictionary<string, string> ToolPathOverrides
    {
        get => _toolPathOverrides ??= new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
        set => _toolPathOverrides = value;
    }

    /// <summary>Effective output folder, with the default applied.</summary>
    public string ResolveOutputDirectory() =>
        string.IsNullOrWhiteSpace(DefaultOutputDirectory)
            ? AppPaths.DefaultOutputDirectory
            : DefaultOutputDirectory;

    /// <summary>Effective temp folder, with the mode and default applied.</summary>
    public string ResolveTempDirectory() =>
        TempStorage == TempStorageMode.CustomFolder && !string.IsNullOrWhiteSpace(CustomTempDirectory)
            ? CustomTempDirectory
            : AppPaths.DefaultTempDirectory;

    /// <summary>
    /// Pulls out-of-range values back into range. Called after loading so a corrupt or
    /// hand-edited file degrades to something usable instead of breaking the app.
    /// </summary>
    public AppSettings Normalize()
    {
        if (!Enum.IsDefined(Theme))
        {
            Theme = ThemeMode.System;
        }

        if (!Enum.IsDefined(TempStorage))
        {
            TempStorage = TempStorageMode.Disk;
        }

        MaxConcurrentJobs = Math.Clamp(MaxConcurrentJobs, 1, MaxConcurrencyLimit);

        return this;
    }

    /// <summary>Upper bound on the concurrency slider — twice the core count, never below 4.</summary>
    public static int MaxConcurrencyLimit => Math.Max(4, Environment.ProcessorCount * 2);

    public AppSettings Clone() => (AppSettings)MemberwiseClone();
}
