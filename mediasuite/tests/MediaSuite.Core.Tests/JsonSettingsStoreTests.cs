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
            WindowLeft = 120,
            WindowTop = 80,
            WindowWidth = 1400,
            WindowHeight = 900,
            WindowMaximized = true,
        });

        var reloaded = new JsonSettingsStore(path).Load();

        Assert.Equal(ThemeMode.Dark, reloaded.Theme);
        Assert.Equal(temp.Path, reloaded.DefaultOutputDirectory);
        Assert.Equal(3, reloaded.MaxConcurrentJobs);
        Assert.True(reloaded.PreserveFolderStructure);
        Assert.False(reloaded.CheckForUpdatesOnLaunch);
        Assert.Equal(temp.Path, reloaded.ResolveTempDirectory());
        Assert.Equal(120, reloaded.WindowLeft);
        Assert.Equal(80, reloaded.WindowTop);
        Assert.Equal(1400, reloaded.WindowWidth);
        Assert.Equal(900, reloaded.WindowHeight);
        Assert.True(reloaded.WindowMaximized);
    }

    [Fact]
    public void A_never_saved_window_position_stays_unset_rather_than_defaulting_to_zero()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");

        // Every other field set, window bounds deliberately left alone — the first-launch
        // case, or an older settings file saved before this existed.
        new JsonSettingsStore(path).Save(new AppSettings { Theme = ThemeMode.Dark });

        var reloaded = new JsonSettingsStore(path).Load();

        Assert.Null(reloaded.WindowLeft);
        Assert.Null(reloaded.WindowTop);
        Assert.Null(reloaded.WindowWidth);
        Assert.Null(reloaded.WindowHeight);
        Assert.False(reloaded.WindowMaximized);
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
    public void A_window_size_smaller_than_the_window_s_own_minimum_is_dropped_on_load()
    {
        using var temp = new TempDirectory();
        var path = temp.Combine("settings.json");
        // Below MainWindow.xaml's own MinWidth/MinHeight (900x620) — WPF would enforce
        // that floor at layout time regardless, but a nonsense value should not round-trip
        // back out to a future save looking like a deliberately-chosen size.
        File.WriteAllText(path, """{ "WindowWidth": 10, "WindowHeight": 10, "WindowLeft": 50, "WindowTop": 50 }""");

        var settings = new JsonSettingsStore(path).Load();

        Assert.Null(settings.WindowWidth);
        Assert.Null(settings.WindowHeight);
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
