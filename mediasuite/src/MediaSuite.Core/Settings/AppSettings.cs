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

    private Dictionary<string, List<CustomPreset>>? _customPresets;

    /// <summary>User-saved Custom-preset option sets, keyed by operation id (e.g. "video.compress").</summary>
    public Dictionary<string, List<CustomPreset>> CustomPresets
    {
        get => _customPresets ??= new Dictionary<string, List<CustomPreset>>(StringComparer.OrdinalIgnoreCase);
        set => _customPresets = value;
    }

    /// <summary>Saved presets for one operation, in save order. Empty when none exist.</summary>
    public IReadOnlyList<CustomPreset> PresetsFor(string operationId) =>
        CustomPresets.TryGetValue(operationId, out var list) ? list : Array.Empty<CustomPreset>();

    /// <summary>
    /// Saves a preset under this operation, replacing any existing preset with the same
    /// name (case-insensitively) so re-saving under a name already in use overwrites it
    /// instead of piling up duplicates.
    /// </summary>
    public void SaveCustomPreset(string operationId, CustomPreset preset)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(operationId);
        ArgumentNullException.ThrowIfNull(preset);
        ArgumentException.ThrowIfNullOrWhiteSpace(preset.Name);

        if (!CustomPresets.TryGetValue(operationId, out var list))
        {
            list = new List<CustomPreset>();
            CustomPresets[operationId] = list;
        }

        var index = list.FindIndex(existing => string.Equals(existing.Name, preset.Name, StringComparison.OrdinalIgnoreCase));
        if (index >= 0)
        {
            list[index] = preset;
        }
        else
        {
            list.Add(preset);
        }
    }

    /// <summary>Removes a saved preset by name. Returns whether anything was removed.</summary>
    public bool DeleteCustomPreset(string operationId, string name)
    {
        if (!CustomPresets.TryGetValue(operationId, out var list))
        {
            return false;
        }

        var removed = list.RemoveAll(preset => string.Equals(preset.Name, name, StringComparison.OrdinalIgnoreCase)) > 0;

        if (removed && list.Count == 0)
        {
            CustomPresets.Remove(operationId);
        }

        return removed;
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

        if (_customPresets is not null)
        {
            foreach (var operationId in _customPresets.Keys.ToList())
            {
                var list = _customPresets[operationId];
                if (list is null)
                {
                    _customPresets.Remove(operationId);
                    continue;
                }

                list.RemoveAll(preset =>
                    preset is null || string.IsNullOrWhiteSpace(preset.Name) || preset.Options is null);

                if (list.Count == 0)
                {
                    _customPresets.Remove(operationId);
                }
            }
        }

        return this;
    }

    /// <summary>Upper bound on the concurrency slider — twice the core count, never below 4.</summary>
    public static int MaxConcurrencyLimit => Math.Max(4, Environment.ProcessorCount * 2);

    public AppSettings Clone() => (AppSettings)MemberwiseClone();
}
