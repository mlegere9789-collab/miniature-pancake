using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class JobLauncherTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    public void Dispose() => _temp.Dispose();

    private JobQueueManager CreateQueue() =>
        new(new EngineRegistry(),
            new DiskTempWorkspaceFactory(_temp.Combine("work")),
            maxConcurrency: 1);

    private static FeatureDescriptor Feature(string operationId = "image.convert") =>
        FeatureCatalog.FromOperationId(operationId)!;

    [Fact]
    public void Each_file_becomes_its_own_job_so_the_queue_can_spread_them_out()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var files = new[] { "a.jpg", "b.jpg", "c.jpg" };

        var jobs = launcher.Launch(Feature(), files, "png", QualityPreset.Balanced);

        Assert.Equal(3, jobs.Count);
        Assert.All(jobs, job => Assert.Single(job.Spec.InputPaths));
        Assert.Equal(files, jobs.Select(job => job.Spec.InputPaths[0]));
    }

    [Fact]
    public void The_chosen_format_preset_and_operation_reach_the_spec()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });

        var job = launcher.Launch(Feature("image.compress.jpeg"), new[] { "a.png" }, "jpg", QualityPreset.Best)[0];

        Assert.Equal("image.compress.jpeg", job.Spec.OperationId);
        Assert.Equal("jpg", job.Spec.Output.Format);
        Assert.Equal(QualityPreset.Best, job.Spec.Preset);
    }

    [Fact]
    public void No_format_chosen_means_the_tool_keeps_the_input_format()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });

        var job = launcher.Launch(Feature("image.resize"), new[] { "a.png" }, null, QualityPreset.Balanced)[0];

        Assert.Null(job.Spec.Output.Format);
    }

    [Fact]
    public void The_default_save_folder_is_used_unless_the_job_overrides_it()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var settings = new AppSettings { DefaultOutputDirectory = _temp.Combine("default") };
        var launcher = new JobLauncher(queue, settings);

        var fromSettings = launcher.Launch(Feature(), new[] { "a.jpg" }, "png", QualityPreset.Balanced)[0];
        var overridden = launcher.Launch(
            Feature(), new[] { "a.jpg" }, "png", QualityPreset.Balanced, _temp.Combine("elsewhere"))[0];

        Assert.Equal(_temp.Combine("default"), fromSettings.Spec.Output.Directory);
        Assert.Equal(_temp.Combine("elsewhere"), overridden.Spec.Output.Directory);
    }

    [Fact]
    public void The_folder_structure_preference_is_carried_onto_every_job()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var settings = new AppSettings
        {
            DefaultOutputDirectory = _temp.Path,
            PreserveFolderStructure = true,
        };

        var jobs = new JobLauncher(queue, settings)
            .Launch(Feature(), new[] { "a.jpg", "b.jpg" }, "png", QualityPreset.Balanced);

        Assert.All(jobs, job => Assert.True(job.Spec.Output.PreserveFolderStructure));
    }

    [Fact]
    public void BatchRoot_is_the_whole_selections_own_common_root_not_each_files_own_folder()
    {
        // Regression test: every non-merging job gets a JobSpec whose own InputPaths has
        // exactly one file (see the test above this one) -- so OutputPathResolver.FindCommonRoot
        // of *that* single-file spec would trivially return the file's own containing
        // folder, silently defeating PreserveFolderStructure for every operation except
        // the handful that combine their whole batch into one spec. JobSpec.BatchRoot
        // exists specifically to carry the *real* common root, computed once here from
        // the whole original selection before it gets split.
        using var queue = CreateQueue();
        queue.Pause();

        var settings = new AppSettings { DefaultOutputDirectory = _temp.Path, PreserveFolderStructure = true };
        var fileA = _temp.CreateFile("2024", "a.jpg");
        var fileB = _temp.CreateFile("2025", "b.jpg");

        var jobs = new JobLauncher(queue, settings).Launch(Feature(), new[] { fileA, fileB }, "png", QualityPreset.Balanced);

        var expectedRoot = _temp.Path;
        Assert.All(jobs, job => Assert.Equal(expectedRoot, job.Spec.BatchRoot, StringComparer.OrdinalIgnoreCase));
    }

    [Fact]
    public void BatchIndex_is_each_files_real_position_in_the_whole_selection()
    {
        // Regression test: the {index} filename template token had the same problem as
        // BatchRoot above, for the same reason -- each per-file spec's own loop position
        // is always 0/1 since JobLauncher already split the batch down to one file per
        // spec by the time any engine sees it.
        using var queue = CreateQueue();
        queue.Pause();

        var settings = new AppSettings { DefaultOutputDirectory = _temp.Path };
        var files = new[] { "a.jpg", "b.jpg", "c.jpg" };

        var jobs = new JobLauncher(queue, settings).Launch(Feature(), files, "png", QualityPreset.Balanced);

        Assert.Equal(new int?[] { 1, 2, 3 }, jobs.Select(job => job.Spec.BatchIndex));
    }

    [Fact]
    public void BatchIndex_is_null_for_a_tool_that_combines_its_own_inputs()
    {
        // That spec's own InputPaths already is the whole batch, so its engine's own
        // loop position is already a correct index -- BatchIndex must stay null rather
        // than forcing every output in a merge (e.g. every frame of a slideshow) to the
        // same fixed position.
        using var queue = CreateQueue();
        queue.Pause();

        var settings = new AppSettings { DefaultOutputDirectory = _temp.Path };
        var job = new JobLauncher(queue, settings)
            .Launch(Feature("pdf.merge"), new[] { "a.pdf", "b.pdf" }, "pdf", QualityPreset.Balanced)[0];

        Assert.Null(job.Spec.BatchIndex);
    }

    [Fact]
    public void A_tool_that_merges_its_inputs_gets_one_job_over_all_of_them()
    {
        // A GIF built from twenty stills is one animation, not twenty jobs racing to write
        // twenty single-frame GIFs over each other.
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var frames = new[] { "01.png", "02.png", "03.png" };

        var job = Assert.Single(launcher.Launch(Feature("gif.from-images"), frames, null, QualityPreset.Balanced));

        Assert.Equal(frames, job.Spec.InputPaths);
    }

    [Fact]
    public void The_order_the_frames_were_added_in_is_the_order_they_play_in()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var frames = new[] { "c.png", "a.png", "b.png" };

        var job = Assert.Single(launcher.Launch(Feature("gif.maker"), frames, null, QualityPreset.Balanced));

        Assert.Equal(frames, job.Spec.InputPaths);
    }

    [Fact]
    public void A_gif_conversion_is_still_one_job_per_file()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });

        var jobs = launcher.Launch(
            Feature("gif.mp4-to-gif"), new[] { "a.mp4", "b.mp4" }, null, QualityPreset.Balanced);

        Assert.Equal(2, jobs.Count);
        Assert.All(jobs, job => Assert.Single(job.Spec.InputPaths));
    }

    [Fact]
    public void Merging_pdfs_is_also_one_job_over_every_file()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var files = new[] { "a.pdf", "b.pdf", "c.pdf" };

        var job = Assert.Single(launcher.Launch(Feature("pdf.merge"), files, null, QualityPreset.Balanced));

        Assert.Equal(files, job.Spec.InputPaths);
    }

    [Fact]
    public void A_merging_tool_with_no_files_still_queues_nothing()
    {
        using var queue = CreateQueue();

        var jobs = new JobLauncher(queue, new AppSettings())
            .Launch(Feature("gif.from-images"), Array.Empty<string>(), null, QualityPreset.Balanced);

        Assert.Empty(jobs);
        Assert.True(queue.IsIdle);
    }

    [Fact]
    public void Launching_nothing_queues_nothing()
    {
        using var queue = CreateQueue();

        var jobs = new JobLauncher(queue, new AppSettings())
            .Launch(Feature(), Array.Empty<string>(), "png", QualityPreset.Balanced);

        Assert.Empty(jobs);
        Assert.True(queue.IsIdle);
    }

    [Fact]
    public void Google_Drive_upload_is_off_by_default()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var job = launcher.Launch(Feature(), new[] { "a.jpg" }, "png", QualityPreset.Balanced)[0];

        Assert.False(job.Spec.Output.UploadToGoogleDrive);
        Assert.Null(job.Spec.Output.GoogleDriveFolderId);
    }

    [Fact]
    public void Requesting_a_Google_Drive_upload_carries_the_folder_id_onto_every_job()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var jobs = launcher.Launch(
            Feature(), new[] { "a.jpg", "b.jpg" }, "png", QualityPreset.Balanced,
            uploadToGoogleDrive: true, googleDriveFolderId: "folder-42");

        Assert.All(jobs, job =>
        {
            Assert.True(job.Spec.Output.UploadToGoogleDrive);
            Assert.Equal("folder-42", job.Spec.Output.GoogleDriveFolderId);
        });
    }

    [Fact]
    public void A_blank_Google_Drive_folder_id_means_Drive_root()
    {
        using var queue = CreateQueue();
        queue.Pause();

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Path });
        var job = launcher.Launch(
            Feature(), new[] { "a.jpg" }, "png", QualityPreset.Balanced,
            uploadToGoogleDrive: true, googleDriveFolderId: "   ")[0];

        Assert.Null(job.Spec.Output.GoogleDriveFolderId);
    }

    [Fact]
    public async Task Launched_jobs_actually_run_on_the_queue()
    {
        var input = _temp.CreateFile("photo.jpg");
        _temp.CreateFile("tools", "imagemagick", "magick.exe");

        var locator = new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty);
        var engines = EngineSetup.CreateDefaultRegistry(new FakeProcessRunner(), locator);

        using var queue = new JobQueueManager(
            engines, new DiskTempWorkspaceFactory(_temp.Combine("work")), maxConcurrency: 2, locator);

        var launcher = new JobLauncher(queue, new AppSettings { DefaultOutputDirectory = _temp.Combine("out") });
        var jobs = launcher.Launch(Feature(), new[] { input }, "png", QualityPreset.Balanced);

        await queue.WaitForIdleAsync().WaitAsync(TimeSpan.FromSeconds(10));

        var job = Assert.Single(jobs);
        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.Equal("photo.png", Path.GetFileName(Assert.Single(job.Result!.OutputPaths)));
    }
}
