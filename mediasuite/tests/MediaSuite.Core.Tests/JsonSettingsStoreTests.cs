using MediaSuite.Core.Settings;
using Xunit;

namespace MediaSuite.Core.Tests;

public class JsonSettingsStoreTests
{
    [Fact]
    public void Load_returns_defaults_when_no_file_exists()
    {
        using var temp = new TempDirectory();
        var store = new JsonSettingsStore(temp.Combine("settings.json"));

        var settings = store.Load();

        Assert.Equal(ThemeMode.System, settings.Theme);
        Assert.Equal(Environment.ProcessorCount, settings.MaxConcurrentJobs);
        Assert.True(settings.CheckForUpdatesOnLaunch);
        Assert.False(settings.GoogleDriveEnabled);
        Assert.Equal(TempStorageMode.Disk, settings.TempStorage);
    }

    [Fact]
    public void Settings_survive_a_save_and_reload()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        var store = new JsonSettingsStore(path);

        store.Save(new AppSettings
        {
            Theme = ThemeMode.Dark,
            DefaultOutputDirectory = temp.Path,
            MaxConcurrentJobs = 3,
            PreserveFolderStructure = true,
            CheckForUpdatesOnLaunch = false,
            TempStorage = TempStorageMode.CustomFolder,
            CustomTempDirectory = temp.Path,
        });

        var reloaded = new JsonSettingsStore(path).Load();

        Assert.Equal(ThemeMode.Dark, reloaded.Theme);
        Assert.Equal(temp.Path, reloaded.DefaultOutputDirectory);
        Assert.Equal(3, reloaded.MaxConcurrentJobs);
        Assert.True(reloaded.PreserveFolderStructure);
        Assert.False(reloaded.CheckForUpdatesOnLaunch);
        Assert.Equal(temp.Path, reloaded.ResolveTempDirectory());
    }

    [Fact]
    public void Enums_are_written_as_names_so_the_file_stays_readable()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");

        new JsonSettingsStore(path).Save(new AppSettings { Theme = ThemeMode.Light });

        Assert.Contains("\"Light\"", File.ReadAllText(path), StringComparison.Ordinal);
    }

    [Fact]
    public void A_corrupt_file_degrades_to_defaults_and_is_kept_for_inspection()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, "{ this is not json");

        var settings = new JsonSettingsStore(path).Load();

        Assert.Equal(ThemeMode.System, settings.Theme);
        Assert.True(File.Exists(temp.Combine("settings.corrupt.json")));
    }

    [Fact]
    public void Out_of_range_values_are_clamped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        File.WriteAllText(path, """{ "MaxConcurrentJobs": 0, "Theme": "Dark" }""");

        var settings = new JsonSettingsStore(path).Load();

        Assert.Equal(1, settings.MaxConcurrentJobs);
        Assert.Equal(ThemeMode.Dark, settings.Theme);
    }

    [Fact]
    public void Saving_twice_replaces_the_file_rather_than_leaving_a_temp_file_behind()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        var store = new JsonSettingsStore(path);

        store.Save(new AppSettings { Theme = ThemeMode.Light });
        store.Save(new AppSettings { Theme = ThemeMode.Dark });

        Assert.Equal(ThemeMode.Dark, store.Load().Theme);
        Assert.False(File.Exists(path + ".tmp"));
    }

    [Fact]
    public void Unset_folders_resolve_to_the_documented_defaults()
    {
        var settings = new AppSettings();

        Assert.Equal(AppPaths.DefaultOutputDirectory, settings.ResolveOutputDirectory());
        Assert.Equal(AppPaths.DefaultTempDirectory, settings.ResolveTempDirectory());
    }

    [Fact]
    public void A_custom_temp_folder_is_ignored_while_the_mode_is_Disk()
    {
        var settings = new AppSettings
        {
            TempStorage = TempStorageMode.Disk,
            CustomTempDirectory = @"R:\ramdisk",
        };

        Assert.Equal(AppPaths.DefaultTempDirectory, settings.ResolveTempDirectory());
    }

    [Fact]
    public void An_unset_Google_Drive_credentials_path_resolves_to_the_documented_default()
    {
        var settings = new AppSettings();

        Assert.Equal(AppPaths.DefaultGoogleDriveCredentialsFile, settings.ResolveGoogleDriveCredentialsPath());
    }

    [Fact]
    public void A_Google_Drive_credentials_override_is_used_once_set()
    {
        var settings = new AppSettings { GoogleDriveCredentialsPath = @"D:\creds\client.json" };

        Assert.Equal(@"D:\creds\client.json", settings.ResolveGoogleDriveCredentialsPath());
    }
}
