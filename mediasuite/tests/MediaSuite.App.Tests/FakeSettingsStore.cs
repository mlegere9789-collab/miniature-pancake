using MediaSuite.Core.Settings;

namespace MediaSuite.App.Tests;

/// <summary>In-memory stand-in for the real disk-backed settings store, for ModulePageViewModelTests.</summary>
public sealed class FakeSettingsStore : ISettingsStore
{
    private AppSettings _settings;

    public FakeSettingsStore(AppSettings settings)
    {
        _settings = settings;
    }

    public int SaveCount { get; private set; }

    public AppSettings Load() => _settings;

    public void Save(AppSettings settings)
    {
        _settings = settings;
        SaveCount++;
    }
}
