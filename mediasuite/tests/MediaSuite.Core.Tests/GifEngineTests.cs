using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class GifEngineTests : IDisposable
{
    private const string ProbeJson = """
    {
      "streams": [{ "codec_name": "h264", "codec_type": "video", "width": 640, "height": 480 }],
      "format": { "duration": "20.0" }
    }
    """;

    private readonly TempDirectory _temp = new();
    private readonly FakeProcessRunner _runner = new() { ProbeJson = ProbeJson };

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(bool ffmpeg = true, bool ffprobe = true)
    {
        if (ffmpeg)
        {
            _temp.CreateFile("tools", "ffmpeg", "ffmpeg.exe");
        }

        if (ffprobe)
        {
            _temp.CreateFile("tools", "ffmpeg", "ffprobe.exe");
        }

        return new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty);
    }

    private JobSpec Spec(
        string operationId,
        IReadOnlyList<string> inputs,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out") },
        WorkingDirectory = _temp.Combine("work"),
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static Task<JobResult> Run(
        GifEngine engine,
        JobSpec spec,
        IProgress<JobProgress>? progress = null,
        CancellationToken cancellationToken = default) =>
        engine.RunAsync(spec, progress ?? new DelegateProgress<JobProgress>(_ => { }), cancellationToken);

    private IReadOnlyList<ProcessRequest> FFmpegCalls() =>
        _runner.Requests.Where(r => r.FileName.Contains("ffmpeg.exe", StringComparison.OrdinalIgnoreCase)).ToList();

    // --- What the engine claims -------------------------------------------

    [Fact]
    public void The_engine_claims_the_gif_operations_and_nothing_else()
    {
        var engine = new GifEngine(_runner, Tools());

        Assert.True(engine.CanHandle(Spec("gif.from-video", new[] { "a.mp4" })));
        Assert.True(engine.CanHandle(Spec("gif.compress", new[] { "a.gif" })));
        Assert.True(engine.CanHandle(Spec("gif.to-mp4", new[] { "a.gif" })));
        Assert.False(engine.CanHandle(Spec("video.convert", new[] { "a.mp4" })));
        Assert.False(engine.CanHandle(Spec("image.convert", new[] { "a.png" })));
    }

    // --- Video to GIF ------------------------------------------------------

    [Fact]
    public async Task Making_a_gif_probes_once_and_then_runs_both_palette_passes()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.from-video", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(_runner.RequestsFor("ffprobe"));

        var calls = FFmpegCalls();
        Assert.Equal(2, calls.Count);
        Assert.Contains("palettegen", string.Join(' ', calls[0].Arguments), StringComparison.Ordinal);
        Assert.Contains("paletteuse", string.Join(' ', calls[1].Arguments), StringComparison.Ordinal);
    }

    [Fact]
    public async Task The_output_is_a_gif_named_after_the_source()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.from-video", new[] { input }));

        var output = Assert.Single(result.OutputPaths);
        Assert.Equal("clip.gif", Path.GetFileName(output));
        Assert.True(File.Exists(output));
    }

    [Fact]
    public async Task A_format_chosen_in_the_picker_cannot_override_what_the_tool_writes()
    {
        // Every GIF tool has exactly one possible output; letting a stale picker value
        // through would name an MP4 ".gif".
        var input = _temp.CreateFile("clip.mp4");
        var engine = new GifEngine(_runner, Tools());

        var spec = Spec("gif.from-video", new[] { input });
        spec = spec with { Output = spec.Output with { Format = "webm" } };

        var result = await Run(engine, spec);

        Assert.Equal("clip.gif", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Every_file_in_a_batch_gets_its_own_palette()
    {
        // Sharing one palette path across a batch would let the second file's palette pass
        // overwrite the first file's while it is still encoding.
        var engine = new GifEngine(_runner, Tools());
        var inputs = new[] { _temp.CreateFile("one.mp4"), _temp.CreateFile("two.mp4") };

        var result = await Run(engine, Spec("gif.from-video", inputs));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(2, result.OutputPaths.Count);

        var palettes = FFmpegCalls()
            .Select(call => call.Arguments[^1])
            .Where(path => path.EndsWith("palette.png", StringComparison.OrdinalIgnoreCase))
            .ToList();

        Assert.Equal(2, palettes.Count);
        Assert.Equal(2, palettes.Distinct(StringComparer.OrdinalIgnoreCase).Count());
    }

    [Fact]
    public async Task Progress_climbs_through_both_passes_and_finishes_at_a_hundred()
    {
        var input = _temp.CreateFile("clip.mp4");
        _runner.EmittedStdoutLines.Add("out_time=00:00:20.000000");

        var seen = new List<JobProgress>();
        var engine = new GifEngine(_runner, Tools());

        await Run(engine, Spec("gif.from-video", new[] { input }),
            new DelegateProgress<JobProgress>(seen.Add));

        // The palette pass finishing is half the job, not all of it.
        Assert.Contains(seen, p => p.Stage == "Choosing colours" && p.Percent is > 0 and <= 50);
        Assert.Contains(seen, p => p.Stage == "Encoding" && p.Percent is > 50);
        Assert.Equal(100, seen[^1].Percent);
    }

    [Fact]
    public async Task A_trimmed_gif_measures_progress_against_the_piece_being_kept()
    {
        var input = _temp.CreateFile("clip.mp4");

        // Three seconds into a five-second cut is most of the way through it, even though
        // it is a sliver of the twenty-second source.
        _runner.EmittedStdoutLines.Add("out_time=00:00:03.000000");

        var seen = new List<JobProgress>();
        var engine = new GifEngine(_runner, Tools());

        await Run(
            engine,
            Spec("gif.from-video", new[] { input }, ("start", "2"), ("duration", "5")),
            new DelegateProgress<JobProgress>(seen.Add));

        Assert.Contains(seen, p => p.Stage == "Choosing colours" && p.Percent is > 25);
    }

    [Fact]
    public async Task Without_ffprobe_the_job_still_runs_with_an_indeterminate_bar()
    {
        var input = _temp.CreateFile("clip.mp4");
        _runner.EmittedStdoutLines.Add("out_time=00:00:05.000000");

        var seen = new List<JobProgress>();
        var engine = new GifEngine(_runner, Tools(ffprobe: false));

        var result = await Run(engine, Spec("gif.from-video", new[] { input }),
            new DelegateProgress<JobProgress>(seen.Add));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Empty(_runner.RequestsFor("ffprobe"));
        Assert.Contains(seen, p => p.Percent is null);
    }

    // --- GIF from images ---------------------------------------------------

    [Fact]
    public async Task A_slideshow_is_one_job_over_every_image_and_writes_a_concat_list()
    {
        var inputs = new[]
        {
            _temp.CreateFile("frames", "01.png"),
            _temp.CreateFile("frames", "02.png"),
            _temp.CreateFile("frames", "03.png"),
        };

        var engine = new GifEngine(_runner, Tools());
        var result = await Run(engine, Spec("gif.from-images", inputs, ("frameDurationMs", "500")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(result.OutputPaths);
        Assert.Equal("01.gif", Path.GetFileName(result.OutputPaths[0]));

        var listPath = Path.Combine(_temp.Combine("work"), "frames.txt");
        var list = await File.ReadAllTextAsync(listPath);

        Assert.All(inputs, path => Assert.Contains(path, list, StringComparison.Ordinal));
        Assert.Contains("duration 0.5", list, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_slideshow_never_asks_ffprobe_about_a_still_image()
    {
        var inputs = new[] { _temp.CreateFile("a.png"), _temp.CreateFile("b.png") };
        var engine = new GifEngine(_runner, Tools());

        await Run(engine, Spec("gif.maker", inputs));

        Assert.Empty(_runner.RequestsFor("ffprobe"));
        Assert.Equal(2, FFmpegCalls().Count);
        Assert.Contains("-f", FFmpegCalls()[0].Arguments);
        Assert.Contains("concat", FFmpegCalls()[0].Arguments);
    }

    [Fact]
    public async Task The_concat_list_has_no_byte_order_mark()
    {
        // The demuxer reads the list as plain text and would take a BOM as part of the
        // first directive, failing on a file that looks perfectly correct.
        var inputs = new[] { _temp.CreateFile("a.png") };

        await Run(new GifEngine(_runner, Tools()), Spec("gif.from-images", inputs));

        var bytes = await File.ReadAllBytesAsync(Path.Combine(_temp.Combine("work"), "frames.txt"));

        Assert.Equal((byte)'f', bytes[0]);
    }

    // --- Leaving GIF behind ------------------------------------------------

    [Fact]
    public async Task Turning_a_gif_into_a_video_is_a_single_pass()
    {
        var input = _temp.CreateFile("loop.gif");
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.to-mp4", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(FFmpegCalls());
        Assert.Equal("loop.mp4", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Turning_a_gif_into_animated_png_writes_an_apng()
    {
        var input = _temp.CreateFile("loop.gif");
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.to-apng", new[] { input }));

        Assert.Equal("loop.apng", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    // --- When things go wrong ----------------------------------------------

    [Fact]
    public async Task A_missing_ffmpeg_is_reported_rather_than_thrown()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new GifEngine(_runner, Tools(ffmpeg: false, ffprobe: false));

        var result = await Run(engine, Spec("gif.from-video", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("FFmpeg", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_file_that_vanished_between_queueing_and_running_fails_the_job()
    {
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.from-video", new[] { _temp.Combine("gone.mp4") }));

        Assert.False(result.IsSuccess);
        Assert.Contains("gone.mp4", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_slideshow_checks_every_image_before_starting_any_work()
    {
        var inputs = new[] { _temp.CreateFile("a.png"), _temp.Combine("missing.png") };
        var engine = new GifEngine(_runner, Tools());

        var result = await Run(engine, Spec("gif.from-images", inputs));

        Assert.False(result.IsSuccess);
        Assert.Contains("missing.png", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(FFmpegCalls());
    }

    [Fact]
    public async Task A_failed_palette_pass_stops_the_job_instead_of_encoding_against_nothing()
    {
        var input = _temp.CreateFile("clip.mp4");

        var runner = new FakeProcessRunner(request =>
            request.FileName.Contains("ffprobe", StringComparison.OrdinalIgnoreCase)
                ? new ProcessResult(0, ProbeJson, string.Empty, TimeSpan.Zero)
                : new ProcessResult(1, string.Empty, "clip.mp4: Invalid data found", TimeSpan.Zero));

        var result = await Run(new GifEngine(runner, Tools()), Spec("gif.from-video", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("Invalid data found", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Contains("choosing colours", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);

        // One ffprobe and one failed pass — the encode was never attempted.
        Assert.Single(runner.Requests.Where(r => r.FileName.Contains("ffmpeg.exe", StringComparison.OrdinalIgnoreCase)));
    }

    [Fact]
    public async Task Cancelling_stops_before_the_next_pass()
    {
        var input = _temp.CreateFile("clip.mp4");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        var engine = new GifEngine(_runner, Tools());

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => Run(engine, Spec("gif.from-video", new[] { input }), cancellationToken: cancellation.Token));
    }
}
