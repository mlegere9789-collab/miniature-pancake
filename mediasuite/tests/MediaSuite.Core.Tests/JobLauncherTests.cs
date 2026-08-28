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
