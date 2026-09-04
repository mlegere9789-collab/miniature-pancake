using System.IO;
using System.Threading.Tasks;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Features;
using MediaSuite.Core.GoogleDrive;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// <see cref="ModulePageViewModel"/> is the actual Convert/Compress/etc. screen: drop
/// files, pick a tool and format, press Start. Driven here through the real
/// <see cref="FeatureCatalog"/> (there is no seam to fake it, and it is stable enough to
/// build tests against — other tests this session already depend on "video.convert"
/// being a real catalogue entry) and a real <see cref="JobLauncher"/>/<see cref="JobQueueManager"/>,
/// with only the engine and the two outside-world dependencies (settings persistence,
/// Google Drive) faked. <see cref="ModulePageViewModel.BrowseOutputDirectoryCommand"/> is
/// not covered — it opens a real <c>Microsoft.Win32.OpenFolderDialog</c>, which nothing
/// here can drive.
/// </summary>
public sealed class ModulePageViewModelTests : IDisposable
{
    private readonly string _workDir = Path.Combine(Path.GetTempPath(), "MediaSuite.App.Tests." + Guid.NewGuid().ToString("N"));

    public void Dispose()
    {
        if (Directory.Exists(_workDir))
        {
            Directory.Delete(_workDir, recursive: true);
        }
    }

    private sealed record Fixture(
        ModulePageViewModel Page,
        JobQueueManager Queue,
        AppSettings Settings,
        FakeSettingsStore Store,
        FakeGoogleDriveClient Drive) : IDisposable
    {
        public void Dispose() => Queue.Dispose();
    }

    private Fixture CreateModulePage(FakeEngine engine, bool googleDriveEnabled = false)
    {
        var settings = new AppSettings { GoogleDriveEnabled = googleDriveEnabled };
        var store = new FakeSettingsStore(settings);
        var drive = new FakeGoogleDriveClient();
        var registry = new EngineRegistry().Register(engine);
        var queue = new JobQueueManager(registry, new DiskTempWorkspaceFactory(_workDir), maxConcurrency: 4);
        var launcher = new JobLauncher(queue, settings);

        var page = new ModulePageViewModel(
            "Convert",
            "\uE8AB",
            FeatureSection.Convert,
            "Convert anything",
            "Video, audio, image and document conversion.",
            registry,
            launcher,
            settings,
            store,
            drive);

        return new Fixture(page, queue, settings, store, drive);
    }

    [Fact]
    public void Only_operations_an_engine_claims_are_offered_as_ready()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));

        Assert.True(fixture.Page.HasReadyFeatures);
        Assert.Contains(fixture.Page.ReadyFeatures, f => f.OperationId == "video.convert");
        Assert.DoesNotContain(fixture.Page.ReadyFeatures, f => f.OperationId == "image.convert");
        Assert.NotNull(fixture.Page.SelectedFeature);
        Assert.Equal("video.convert", fixture.Page.SelectedFeature!.OperationId);

        // The card itself is still listed (so the user can see what's coming), just not ready.
        var imageFeature = fixture.Page.FeatureGroups
            .SelectMany(g => g.Features)
            .Single(f => f.OperationId == "image.convert");
        Assert.False(imageFeature.IsAvailable);
        Assert.StartsWith("Build step", imageFeature.StatusLabel);
    }

    [Fact]
    public void No_ready_features_means_nothing_is_selected_and_Start_stays_off()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("nothing.in.this.catalog"));

        Assert.False(fixture.Page.HasReadyFeatures);
        Assert.Null(fixture.Page.SelectedFeature);
        Assert.False(fixture.Page.CanStart);
        Assert.Equal("Start stays off until a tool above is ready to run.", fixture.Page.StartHint);
    }

    [Fact]
    public void Adding_files_updates_the_staged_summary_and_enables_Start()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));
        Directory.CreateDirectory(_workDir);
        var filePath = Path.Combine(_workDir, "clip.mp4");
        File.WriteAllBytes(filePath, new byte[1024]);

        Assert.Equal("No files staged", fixture.Page.StagedSummary);
        Assert.False(fixture.Page.CanStart);

        fixture.Page.AddFilesCommand.Execute(new[] { filePath });

        Assert.True(fixture.Page.HasStagedFiles);
        Assert.True(fixture.Page.CanStart);
        Assert.Equal("1 file · 1 KB", fixture.Page.StagedSummary);
        Assert.Contains("Video Converter", fixture.Page.StartHint);
    }

    [Fact]
    public void Duplicate_files_are_only_staged_once()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));

        fixture.Page.AddFilesCommand.Execute(new[] { @"C:\videos\clip.mp4", @"C:\VIDEOS\CLIP.MP4" });

        Assert.Single(fixture.Page.StagedFiles);
    }

    [Fact]
    public void ClearFilesCommand_empties_the_staged_list()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));
        fixture.Page.AddFilesCommand.Execute(new[] { @"C:\videos\clip.mp4" });
        Assert.True(fixture.Page.ClearFilesCommand.CanExecute(null));

        fixture.Page.ClearFilesCommand.Execute(null);

        Assert.False(fixture.Page.HasStagedFiles);
        Assert.False(fixture.Page.ClearFilesCommand.CanExecute(null));
    }

    [Fact]
    public void Starting_queues_one_job_per_file_and_clears_the_staging_area()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));

        // Paused so EnqueueRange leaves every job pending instead of racing a real engine
        // run on a thread-pool thread -- nothing here needs to observe a job actually
        // finish, only that Start queued the right ones.
        fixture.Queue.Pause();
        fixture.Page.AddFilesCommand.Execute(new[] { @"C:\videos\a.mp4", @"C:\videos\b.mp4" });

        fixture.Page.StartCommand.Execute(null);

        Assert.Equal(2, fixture.Queue.Jobs.Count);
        Assert.All(fixture.Queue.Jobs, job => Assert.Equal("video.convert", job.Spec.OperationId));
        Assert.Equal(
            new[] { @"C:\videos\a.mp4", @"C:\videos\b.mp4" },
            fixture.Queue.Jobs.Select(job => job.Spec.InputPaths.Single()));
        Assert.False(fixture.Page.HasStagedFiles);
        Assert.Equal("Queued 2 jobs — Video Converter.", fixture.Page.LastLaunchSummary);
    }

    [Fact]
    public void Selecting_a_different_feature_refreshes_output_formats_and_resets_the_preset_editor()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert", "video.mp4-to-mp3"));
        fixture.Page.AdvancedOptionsText = "crf=18";
        fixture.Page.NewPresetName = "Draft";

        var mp4ToMp3 = fixture.Page.ReadyFeatures.Single(f => f.OperationId == "video.mp4-to-mp3");
        fixture.Page.SelectedFeature = mp4ToMp3;

        // video.mp4-to-mp3 forces its output format -- there is nothing for the user to
        // choose, so the picker offers exactly (and only) that one format.
        Assert.Equal(new[] { "mp3" }, fixture.Page.OutputFormats);
        Assert.Equal("mp3", fixture.Page.SelectedOutputFormat);
        Assert.Equal(string.Empty, fixture.Page.AdvancedOptionsText);
        Assert.Equal(string.Empty, fixture.Page.NewPresetName);
        Assert.Null(fixture.Page.SelectedSavedPreset);
    }

    [Fact]
    public void Video_convert_does_not_offer_Same_as_input_since_it_always_re_encodes()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));

        Assert.DoesNotContain(ModulePageViewModel.SameAsInput, fixture.Page.OutputFormats);
        Assert.NotEmpty(fixture.Page.OutputFormats);
        Assert.Equal(fixture.Page.OutputFormats[0], fixture.Page.SelectedOutputFormat);
    }

    [Fact]
    public void SavePreset_then_reselecting_it_restores_the_advanced_options_text()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));
        fixture.Page.AdvancedOptionsText = "crf=18\nspeed=fast";
        fixture.Page.NewPresetName = "My preset";

        Assert.True(fixture.Page.SavePresetCommand.CanExecute(null));
        fixture.Page.SavePresetCommand.Execute(null);

        var saved = Assert.Single(fixture.Page.SavedPresets);
        Assert.Equal("My preset", saved.Name);
        Assert.Equal("Saved preset \"My preset\".", fixture.Page.PresetFeedback);
        Assert.Same(saved, fixture.Page.SelectedSavedPreset);
        Assert.Equal(1, fixture.Store.SaveCount);

        fixture.Page.AdvancedOptionsText = string.Empty;
        fixture.Page.NewPresetName = string.Empty;
        fixture.Page.SelectedSavedPreset = null;

        fixture.Page.SelectedSavedPreset = saved;

        Assert.Equal("crf=18\nspeed=fast", fixture.Page.AdvancedOptionsText);
        Assert.Equal("My preset", fixture.Page.NewPresetName);
    }

    [Fact]
    public void DeletePreset_removes_it_and_clears_the_editor()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"));
        fixture.Page.AdvancedOptionsText = "crf=18";
        fixture.Page.NewPresetName = "Throwaway";
        fixture.Page.SavePresetCommand.Execute(null);
        Assert.True(fixture.Page.DeletePresetCommand.CanExecute(null));

        fixture.Page.DeletePresetCommand.Execute(null);

        Assert.Empty(fixture.Page.SavedPresets);
        Assert.Equal(string.Empty, fixture.Page.AdvancedOptionsText);
        Assert.Equal(string.Empty, fixture.Page.NewPresetName);
        Assert.Null(fixture.Page.SelectedSavedPreset);
        Assert.Equal("Deleted preset \"Throwaway\".", fixture.Page.PresetFeedback);
    }

    [Fact]
    public void Turning_on_upload_loads_and_auto_selects_the_last_used_folder()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"), googleDriveEnabled: true);
        fixture.Settings.LastGoogleDriveFolderId = "42";
        fixture.Drive.FoldersToReturn = new[]
        {
            new GoogleDriveFolder { Id = "1", Name = "Clips" },
            new GoogleDriveFolder { Id = "42", Name = "Exports" },
        };

        fixture.Page.UploadToGoogleDrive = true;

        Assert.Equal(2, fixture.Page.DriveFolders.Count);
        Assert.NotNull(fixture.Page.SelectedDriveFolder);
        Assert.Equal("42", fixture.Page.SelectedDriveFolder!.Id);
        Assert.False(fixture.Page.HasDriveFolderHint);
    }

    [Fact]
    public void No_folders_yet_is_a_hint_not_an_error()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"), googleDriveEnabled: true);

        fixture.Page.UploadToGoogleDrive = true;

        Assert.Empty(fixture.Page.DriveFolders);
        Assert.Equal("No folders yet — Drive root will be used.", fixture.Page.DriveFolderHint);
    }

    [Fact]
    public void Not_signed_in_points_at_Settings_instead_of_the_raw_exception()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"), googleDriveEnabled: true);
        fixture.Drive.ListFoldersFailure = new GoogleDriveNotSignedInException();

        fixture.Page.UploadToGoogleDrive = true;

        Assert.Equal("Sign in to Google Drive from Settings to pick a folder.", fixture.Page.DriveFolderHint);
    }

    [Fact]
    public void CreateDriveFolderCommand_adds_and_selects_the_new_folder()
    {
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"), googleDriveEnabled: true);
        fixture.Page.NewDriveFolderName = "  New Exports  ";
        Assert.True(fixture.Page.CreateDriveFolderCommand.CanExecute(null));

        fixture.Page.CreateDriveFolderCommand.Execute(null);

        var created = Assert.Single(fixture.Drive.CreatedFolders);
        Assert.Equal("New Exports", created.Name);
        Assert.Equal("New Exports", Assert.Single(fixture.Page.DriveFolders).Name);
        Assert.Same(fixture.Page.DriveFolders[0], fixture.Page.SelectedDriveFolder);
        Assert.Equal(string.Empty, fixture.Page.NewDriveFolderName);
    }

    [Fact]
    public void CreateDriveFolderCommand_cannot_fire_a_second_time_while_the_first_call_is_still_in_flight()
    {
        // CanExecute only re-queries on the usual WPF input events, not the instant
        // NewDriveFolderName changes -- and it only clears on success, after the awaited
        // Drive call returns. Without an explicit busy guard, a double-click on "New folder"
        // would fire CreateFolderAsync twice concurrently and create two identical folders.
        using var fixture = CreateModulePage(FakeEngine.HandlesOnly("video.convert"), googleDriveEnabled: true);
        fixture.Page.NewDriveFolderName = "Vacation Photos";
        var pending = new TaskCompletionSource<string>();
        fixture.Drive.PendingCreateFolder = pending;

        Assert.True(fixture.Page.CreateDriveFolderCommand.CanExecute(null));
        fixture.Page.CreateDriveFolderCommand.Execute(null);

        Assert.False(fixture.Page.CreateDriveFolderCommand.CanExecute(null));

        pending.SetResult("folder-id");

        Assert.Single(fixture.Drive.CreatedFolders);
        Assert.Equal(string.Empty, fixture.Page.NewDriveFolderName);
    }
}
