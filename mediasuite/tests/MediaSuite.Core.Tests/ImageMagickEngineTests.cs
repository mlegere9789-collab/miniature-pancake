using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ImageMagickEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly FakeProcessRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    /// <summary>Locator that finds whichever binaries the test says are installed.</summary>
    private ToolLocator Tools(bool imageMagick = true, bool libRaw = true, bool potrace = false)
    {
        if (imageMagick)
        {
            _temp.CreateFile("tools", "imagemagick", "magick.exe");
        }

        if (libRaw)
        {
            _temp.CreateFile("tools", "libraw", "dcraw_emu.exe");
        }

        if (potrace)
        {
            _temp.CreateFile("tools", "potrace", "potrace.exe");
        }

        return new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty);
    }

    private JobSpec Spec(
        string operationId,
        IReadOnlyList<string> inputs,
        string? format = null,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out"), Format = format },
        WorkingDirectory = _temp.Combine("work"),
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static Task<JobResult> Run(ImageMagickEngine engine, JobSpec spec, IProgress<JobProgress>? progress = null) =>
        engine.RunAsync(spec, progress ?? new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    [Fact]
    public void The_engine_claims_every_image_operation_and_nothing_else()
    {
        var engine = new ImageMagickEngine(_runner, Tools());

        Assert.True(engine.CanHandle(Spec("image.convert", new[] { "a.png" })));
        Assert.True(engine.CanHandle(Spec("image.compress.jpeg", new[] { "a.jpg" })));
        Assert.True(engine.CanHandle(Spec("image.rotate", new[] { "a.jpg" })));
        Assert.False(engine.CanHandle(Spec("video.convert", new[] { "a.mp4" })));
        Assert.False(engine.CanHandle(Spec("image.png-to-svg", new[] { "a.png" })));
    }

    [Fact]
    public async Task Converting_one_file_runs_the_tool_once_and_returns_the_output()
    {
        var input = _temp.CreateFile("photo.jpg");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "png"));

        Assert.True(result.IsSuccess);
        var output = Assert.Single(result.OutputPaths);
        Assert.Equal("photo.png", Path.GetFileName(output));
        Assert.True(File.Exists(output));
        Assert.Single(_runner.Requests);
    }

    [Fact]
    public async Task A_batch_converts_every_file()
    {
        var inputs = new[] { _temp.CreateFile("a.jpg"), _temp.CreateFile("b.jpg"), _temp.CreateFile("c.jpg") };
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.convert", inputs, format: "webp"));

        Assert.True(result.IsSuccess);
        Assert.Equal(3, result.OutputPaths.Count);
        Assert.Equal(3, _runner.Requests.Count);
        Assert.All(result.OutputPaths, path => Assert.EndsWith(".webp", path, StringComparison.Ordinal));
    }

    [Fact]
    public async Task Progress_names_the_file_being_worked_on_and_finishes_at_100()
    {
        var inputs = new[] { _temp.CreateFile("a.jpg"), _temp.CreateFile("b.jpg") };
        var engine = new ImageMagickEngine(_runner, Tools());
        var ticks = new List<JobProgress>();

        await Run(engine, Spec("image.convert", inputs, format: "png"),
            new DelegateProgress<JobProgress>(tick => ticks.Add(tick)));

        Assert.Contains(ticks, tick => tick.CurrentItem == "a.jpg");
        Assert.Contains(ticks, tick => tick.CurrentItem == "b.jpg");
        Assert.Equal(100, ticks[^1].Percent);
    }

    [Fact]
    public async Task A_shortcut_conversion_forces_its_own_output_format()
    {
        var input = _temp.CreateFile("phone.heic");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.heic-to-jpg", new[] { input }));

        Assert.Equal("phone.jpg", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task An_edit_keeps_the_source_format_when_none_is_chosen()
    {
        var input = _temp.CreateFile("photo.png");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.rotate", new[] { input }, options: ("angle", "90")));

        Assert.Equal("photo.png", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task An_explicit_format_beats_the_one_the_operation_would_pick()
    {
        var input = _temp.CreateFile("phone.heic");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.heic-to-jpg", new[] { input }, format: "webp"));

        Assert.Equal("phone.webp", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task A_raw_file_is_developed_by_LibRaw_before_ImageMagick_sees_it()
    {
        var input = _temp.CreateFile("DSC_0001.NEF");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "jpg"));

        Assert.True(result.IsSuccess, result.ErrorMessage);

        var decode = Assert.Single(_runner.RequestsFor("dcraw"));
        Assert.Contains("-T", decode.Arguments);
        Assert.Contains("-w", decode.Arguments);

        // ImageMagick must be handed the decoded file, never the RAW itself.
        var convert = Assert.Single(_runner.RequestsFor("magick"));
        Assert.DoesNotContain(input, convert.Arguments);
        Assert.Contains(convert.Arguments, argument => argument.EndsWith(".tiff", StringComparison.OrdinalIgnoreCase));
    }

    [Fact]
    public async Task Developing_a_raw_leaves_the_photo_library_untouched()
    {
        // dcraw_emu writes beside its input, so the engine works on a copy.
        var input = _temp.CreateFile("originals", "DSC_0001.NEF");
        var engine = new ImageMagickEngine(_runner, Tools());

        await Run(engine, Spec("image.convert", new[] { input }, format: "jpg"));

        var strays = Directory.GetFiles(_temp.Combine("originals"));
        Assert.Equal(new[] { input }, strays);
    }

    [Fact]
    public async Task Editing_a_raw_lands_as_tiff_because_raw_cannot_be_written_back()
    {
        var input = _temp.CreateFile("DSC_0002.CR2");
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.resize", new[] { input }, options: ("width", "1200")));

        Assert.Equal("DSC_0002.tiff", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task A_raw_file_without_LibRaw_fails_and_names_the_missing_tool()
    {
        var input = _temp.CreateFile("DSC_0003.ARW");
        var engine = new ImageMagickEngine(_runner, Tools(libRaw: false));

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "jpg"));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("LibRaw", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task An_ordinary_image_still_converts_with_LibRaw_missing()
    {
        var input = _temp.CreateFile("photo.jpg");
        var engine = new ImageMagickEngine(_runner, Tools(libRaw: false));

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "png"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
    }

    [Fact]
    public async Task Without_ImageMagick_the_job_fails_with_where_to_put_it()
    {
        var input = _temp.CreateFile("photo.jpg");
        var engine = new ImageMagickEngine(_runner, Tools(imageMagick: false));

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "png"));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("ImageMagick", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Contains("tools", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_file_that_disappeared_between_dropping_and_running_fails_by_name()
    {
        var engine = new ImageMagickEngine(_runner, Tools());

        var result = await Run(engine, Spec("image.convert", new[] { _temp.Combine("ghost.jpg") }, format: "png"));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("ghost.jpg", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_failing_tool_reports_what_it_said_rather_than_an_exit_code()
    {
        var input = _temp.CreateFile("broken.jpg");
        var failing = new FakeProcessRunner(_ =>
            new ProcessResult(1, string.Empty, "magick: improper image header", TimeSpan.Zero));
        var engine = new ImageMagickEngine(failing, Tools());

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "png"));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("improper image header", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_tool_that_claims_success_but_writes_nothing_is_still_a_failure()
    {
        var input = _temp.CreateFile("photo.jpg");
        var liar = new FakeProcessRunner(_ => new ProcessResult(0, string.Empty, string.Empty, TimeSpan.Zero));
        var engine = new ImageMagickEngine(liar, Tools());

        var result = await Run(engine, Spec("image.convert", new[] { input }, format: "png"));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("wrote nothing", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_stops_the_batch_partway_through()
    {
        var inputs = Enumerable.Range(0, 6).Select(i => _temp.CreateFile($"{i}.jpg")).ToArray();
        using var cancellation = new CancellationTokenSource();

        var runner = new FakeProcessRunner(request =>
        {
            File.WriteAllText(request.Arguments[^1], "x");
            cancellation.Cancel();
            return new ProcessResult(0, string.Empty, string.Empty, TimeSpan.Zero);
        });

        var engine = new ImageMagickEngine(runner, Tools());

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => engine.RunAsync(
            Spec("image.convert", inputs, format: "png"),
            new DelegateProgress<JobProgress>(_ => { }),
            cancellation.Token));

        Assert.True(runner.Requests.Count < inputs.Length, "the batch should stop, not run to the end");
    }

    [Fact]
    public async Task Output_names_never_collide_within_a_batch()
    {
        // Same file name in different folders is the classic way a batch quietly
        // overwrites its own results.
        var inputs = new[]
        {
            _temp.CreateFile("a", "photo.jpg"),
            _temp.CreateFile("b", "photo.jpg"),
        };

        var engine = new ImageMagickEngine(_runner, Tools());
        var result = await Run(engine, Spec("image.convert", inputs, format: "png"));

        Assert.Equal(2, result.OutputPaths.Distinct(StringComparer.OrdinalIgnoreCase).Count());
        Assert.All(result.OutputPaths, path => Assert.True(File.Exists(path)));
    }

    [Fact]
    public void A_conversion_with_no_format_chosen_is_rejected_before_it_runs()
    {
        var spec = Spec("image.convert", new[] { "photo.jpg" });

        Assert.Throws<ArgumentException>(() => ImageMagickEngine.ResolveOutputFormat(spec, "photo.jpg"));
    }
}
