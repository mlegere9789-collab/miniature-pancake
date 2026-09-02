namespace MediaSuite.Core.Settings;

/// <summary>Loads and saves <see cref="AppSettings"/>.</summary>
public interface ISettingsStore
{
    /// <summary>Reads settings from disk, falling back to defaults when the file is absent or unreadable.</summary>
    AppSettings Load();

    /// <summary>Writes settings to disk.</summary>
    void Save(AppSettings settings);
}
