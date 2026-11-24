using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class FFmpegEngineTests : IDisposable
{
    private const string ProbeJson = """
    {
      "streams": [
        { "codec_name": "h264", "codec_type": "video", "width": 1920, "height": 1080 },
        { "codec_name": "aac", "codec_type": "audio" }
      ],
      "format": { "duration": "100.0" }
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
        string? format = null,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out"), Format = format },
        WorkingDirectory = _temp.Combine("work"),
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static Task<JobResult> Run(
        FFmpegEngine engine,
        JobSpec spec,
        IProgress<JobProgress>? progress = null,
        CancellationToken cancellationToken = default) =>
        engine.RunAsync(spec, progress ?? new DelegateProgress<JobProgress>(_ => { }), cancellationToken);

    [Fact]
    public void The_engine_claims_the_video_and_audio_operations_and_nothing_else()
    {
        var engine = new FFmpegEngine(_runner, Tools());

        Assert.True(engine.CanHandle(Spec("video.convert", new[] { "a.mp4" })));
        Assert.True(engine.CanHandle(Spec("audio.compress.wav", new[] { "a.wav" })));
        Assert.True(engine.CanHandle(Spec("video.trim", new[] { "a.mp4" })));
        Assert.False(engine.CanHandle(Spec("image.convert", new[] { "a.png" })));
        Assert.False(engine.CanHandle(Spec("pdf.merge", new[] { "a.pdf" })));
    }

    [Fact]
    public async Task Converting_probes_first_then_encodes()
    {
        var input = _temp.CreateFile("holiday.mov");
        var engine = new FFmpegEngine(_runner, Tools());

        var result = await Run(engine, Spec("video.convert", new[] { input }, format: "mp4"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("holiday.mp4", Path.GetFileName(Assert.Single(result.OutputPaths)));

        Assert.Equal(2, _runner.Requests.Count);
        Assert.Contains("ffprobe", _runner.Requests[0].FileName, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("ffmpeg", _runner.Requests[1].FileName, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task The_probe_never_writes_over_the_file_it_is_reading()
    {
        var input = _temp.CreateFile("holiday.mov");
        File.WriteAllText(input, "original bytes");

        var engine = new FFmpegEngine(_runner, Tools());
        await Run(engine, Spec("video.convert", new[] { input }, format: "mp4"));

        Assert.Equal("original bytes", File.ReadAllText(input));
    }

    [Fact]
    public async Task A_shortcut_conversion_forces_its_own_format()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new FFmpegEngine(_runner, Tools());

        var result = await Run(engine, Spec("video.mp4-to-mp3", new[] { input }));

        Assert.Equal("clip.mp3", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task An_edit_keeps_the_source_container()
    {
        var input = _temp.CreateFile("clip.mkv");
        var engine = new FFmpegEngine(_runner, Tools());

        var result = await Run(engine, Spec("video.compress", new[] { input }));

        Assert.Equal("clip.mkv", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Progress_is_a_percentage_of_the_probed_duration()
    {
        var input = _temp.CreateFile("clip.mp4");
        _runner.EmittedStdoutLines.AddRange(new[]
        {
            "out_time=00:00:25.000",
            "out_time=00:00:50.000",
            "progress=end",
        });

        var engine = new FFmpegEngine(_runner, Tools());
        var ticks = new List<JobProgress>();

        await Run(engine, Spec("video.compress", new[] { input }),
            new DelegateProgress<JobProgress>(tick => ticks.Add(tick)));

        // 25 s and 50 s of a 100 s file, as the only job in the batch.
        Assert.Contains(ticks, tick => tick.Percent is > 24 and < 26);
        Assert.Contains(ticks, tick => tick.Percent is > 49 and < 51);
        Assert.Equal(100, ticks[^1].Percent);
    }

    [Fact]
    public async Task Progress_spans_the_whole_batch_rather_than_restarting_per_file()
    {
        var inputs = new[] { _temp.CreateFile("a.mp4"), _temp.CreateFile("b.mp4") };
        _runner.EmittedStdoutLines.Add("out_time=00:00:50.000");

        var engine = new FFmpegEngine(_runner, Tools());
        var ticks = new List<JobProgress>();

        await Run(engine, Spec("video.compress", inputs),
            new DelegateProgress<JobProgress>(tick => ticks.Add(tick)));

        // Halfway through the first of two files is a quarter of the way through the job.
        Assert.Contains(ticks, tick => tick.Percent is > 24 and < 26);
        Assert.Contains(ticks, tick => tick.Percent is > 74 and < 76);
    }

    [Fact]
    public async Task Without_a_duration_progress_stays_indeterminate_instead_of_lying()
    {
        var input = _temp.CreateFile("clip.mp4");
        _runner.ProbeJson = "{}";
        _runner.EmittedStdoutLines.Add("out_time=00:00:25.000");

        var engine = new FFmpegEngine(_runner, Tools());
        var ticks = new List<JobProgress>();

        await Run(engine, Spec("video.compress", new[] { input }),
            new DelegateProgress<JobProgress>(tick => ticks.Add(tick)));

        Assert.Contains(ticks, tick => tick.Percent is null && tick.Stage == "Compressing");
    }

    [Fact]
    public async Task A_missing_FFprobe_costs_the_percentage_but_not_the_conversion()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new FFmpegEngine(_runner, Tools(ffprobe: false));

        var result = await Run(engine, Spec("video.compress", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(_runner.Requests);
        Assert.Contains("ffmpeg", _runner.LastRequest.FileName, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Without_FFmpeg_the_job_fails_with_where_to_put_it()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new FFmpegEngine(_runner, Tools(ffmpeg: false));

        var result = await Run(engine, Spec("video.compress", new[] { input }));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("FFmpeg", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Contains("tools", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_failing_encode_reports_what_FFmpeg_said()
    {
        var input = _temp.CreateFile("broken.mp4");
        var failing = new FakeProcessRunner(request =>
            request.FileName.Contains("ffprobe", StringComparison.OrdinalIgnoreCase)
                ? new ProcessResult(0, ProbeJson, string.Empty, TimeSpan.Zero)
                : new ProcessResult(1, string.Empty, "Invalid data found when processing input", TimeSpan.Zero));

        var engine = new FFmpegEngine(failing, Tools());
        var result = await Run(engine, Spec("video.compress", new[] { input }));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("Invalid data found", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Contains("broken.mp4", result.ErrorMessage!, StringComparison.Ordinal);

        // ErrorMessage is deliberately one line; Diagnostics is where the queue panel's
        // "Copy details" button gets the full stderr FFmpeg actually wrote, not just the
        // one summarised line.
        Assert.Contains("Invalid data found when processing input", result.Diagnostics!, StringComparison.Ordinal);
        Assert.Contains("Exit code: 1", result.Diagnostics!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task An_encode_that_writes_nothing_is_still_a_failure()
    {
        var input = _temp.CreateFile("clip.mp4");
        var liar = new FakeProcessRunner(request =>
            request.FileName.Contains("ffprobe", StringComparison.OrdinalIgnoreCase)
                ? new ProcessResult(0, ProbeJson, string.Empty, TimeSpan.Zero)
                : new ProcessResult(0, string.Empty, string.Empty, TimeSpan.Zero));

        var engine = new FFmpegEngine(liar, Tools());
        var result = await Run(engine, Spec("video.compress", new[] { input }));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("wrote nothing", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_file_that_disappeared_fails_by_name()
    {
        var engine = new FFmpegEngine(_runner, Tools());

        var result = await Run(engine, Spec("video.compress", new[] { _temp.Combine("ghost.mp4") }));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("ghost.mp4", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_stops_the_batch_partway_through()
    {
        var inputs = Enumerable.Range(0, 6).Select(i => _temp.CreateFile($"{i}.mp4")).ToArray();
        using var cancellation = new CancellationTokenSource();

        var runner = new FakeProcessRunner(request =>
        {
            if (request.FileName.Contains("ffprobe", StringComparison.OrdinalIgnoreCase))
            {
                return new ProcessResult(0, ProbeJson, string.Empty, TimeSpan.Zero);
            }

            File.WriteAllText(request.Arguments[^1], "x");
            cancellation.Cancel();
            return new ProcessResult(0, string.Empty, string.Empty, TimeSpan.Zero);
        });

        var engine = new FFmpegEngine(runner, Tools());

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => Run(engine, Spec("video.compress", inputs), cancellationToken: cancellation.Token));

        Assert.True(runner.Requests.Count < inputs.Length * 2, "the batch should stop, not run to the end");
    }

    [Fact]
    public async Task Cancelling_mid_encode_deletes_the_truncated_output_file()
    {
        var input = _temp.CreateFile("holiday.mov");
        var outputPath = Path.Combine(_temp.Combine("out"), "holiday.mp4");

        var runner = new FakeProcessRunner(request =>
        {
            if (request.FileName.Contains("ffprobe", StringComparison.OrdinalIgnoreCase))
            {
                return new ProcessResult(0, ProbeJson, string.Empty, TimeSpan.Zero);
            }

            // Mirrors what the real ProcessRunner sees: WaitForExitAsync's cancellation
            // lands after the OS process has already written some of the output file, so
            // the file on disk is real by the time it gets killed -- just truncated.
            File.WriteAllText(request.Arguments[^1], "a truncated, half-encoded file");
            throw new OperationCanceledException();
        });

        var engine = new FFmpegEngine(runner, Tools());

        await Assert.ThrowsAnyAsync<OperationCanceledException>(
            () => Run(engine, Spec("video.convert", new[] { input }, format: "mp4")));

        Assert.False(File.Exists(outputPath), "a cancelled job must not leave a truncated file at the final output path");
    }

    [Fact]
    public async Task A_size_target_reaches_the_command_line_using_the_probed_duration()
    {
        var input = _temp.CreateFile("clip.mp4");
        var engine = new FFmpegEngine(_runner, Tools());

        await Run(engine, Spec("video.compress", new[] { input }, options: ("targetSizeMb", "10")));

        // 10 MB over the probed 100 s, less 128 kbps of audio.
        Assert.Contains("691k", _runner.LastRequest.Arguments);
    }

    [Fact]
    public void A_trim_measures_progress_against_the_piece_being_kept()
    {
        var probe = new MediaProbe(TimeSpan.FromMinutes(10));
        var spec = Spec("video.trim", new[] { "a.mp4" }, options: new[] { ("start", "60"), ("end", "90") });

        // Thirty seconds out of a ten-minute source: measuring against the source would
        // leave the bar stuck near 5%.
        Assert.Equal(TimeSpan.FromSeconds(30), FFmpegEngine.ExpectedOutputDuration(spec, probe));
    }

    [Fact]
    public void Anything_other_than_a_trim_measures_against_the_whole_file()
    {
        var probe = new MediaProbe(TimeSpan.FromMinutes(10));

        Assert.Equal(
            TimeSpan.FromMinutes(10),
            FFmpegEngine.ExpectedOutputDuration(Spec("video.compress", new[] { "a.mp4" }), probe));
    }

    [Fact]
    public void The_duration_is_only_injected_when_a_size_target_needs_it()
    {
        var probe = new MediaProbe(TimeSpan.FromSeconds(120));

        var plain = Spec("video.compress", new[] { "a.mp4" });
        Assert.Null(FFmpegEngine.WithDurationForBitrateTargeting(plain, probe).GetOption("durationSeconds"));

        var targeted = Spec("video.compress", new[] { "a.mp4" }, options: ("targetSizeMb", "5"));
        Assert.Equal("120", FFmpegEngine.WithDurationForBitrateTargeting(targeted, probe).GetOption("durationSeconds"));

        Assert.Null(FFmpegEngine.WithDurationForBitrateTargeting(targeted, MediaProbe.Unknown).GetOption("durationSeconds"));
    }
}
