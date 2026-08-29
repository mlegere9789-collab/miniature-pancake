using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class UpscaleEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly UpscaleFakeToolRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(bool realEsrgan = true, bool imageMagick = true)
    {
        if (realEsrgan)
        {
            _temp.CreateFile("tools", "realesrgan", "realesrgan-ncnn-vulkan.exe");
        }

        if (imageMagick)
        {
            _temp.CreateFile("tools", "imagemagick", "magick.exe");
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

    private static Task<JobResult> Run(UpscaleEngine engine, JobSpec spec) =>
        engine.RunAsync(spec, new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    // --- What the engine claims ----------------------------------------------------------

    [Fact]
    public void The_engine_claims_upscale_photo_and_nothing_else()
    {
        var engine = new UpscaleEngine(_runner, Tools());

        Assert.True(engine.CanHandle(Spec("upscale.photo", new[] { "a.png" })));
        Assert.False(engine.CanHandle(Spec("image.enlarge", new[] { "a.png" })));
    }

    [Fact]
    public void Realesrgan_is_required_up_front()
    {
        Assert.Equal(new[] { ExternalToolId.RealEsrgan }, new UpscaleEngine(_runner, Tools()).RequiredTools);
    }

    // --- Ordinary upscaling --------------------------------------------------------------

    [Fact]
    public async Task Upscaling_keeps_the_input_format_by_default()
    {
        var input = _temp.CreateFile("photo.png");
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("photo.png", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task An_explicitly_chosen_format_overrides_the_input_format()
    {
        var input = _temp.CreateFile("photo.png");
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }, format: "webp"));

        Assert.Equal("photo.webp", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Two_and_four_x_are_a_single_pass()
    {
        var input = _temp.CreateFile("a.png");
        var engine = new UpscaleEngine(_runner, Tools());

        await Run(engine, Spec("upscale.photo", new[] { input }, options: ("scale", "2")));
        Assert.Single(_runner.RequestsFor("realesrgan"));

        await Run(engine, Spec("upscale.photo", new[] { input }, options: ("scale", "4")));
        Assert.Equal(2, _runner.RequestsFor("realesrgan").Count);
    }

    [Fact]
    public async Task Eight_x_runs_two_passes_at_four_then_two()
    {
        var input = _temp.CreateFile("a.png");
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }, options: ("scale", "8")));

        Assert.True(result.IsSuccess, result.ErrorMessage);

        var calls = _runner.RequestsFor("realesrgan");
        Assert.Equal(2, calls.Count);
        Assert.Equal("4", calls[0].Arguments[calls[0].Arguments.ToList().IndexOf("-s") + 1]);
        Assert.Equal("2", calls[1].Arguments[calls[1].Arguments.ToList().IndexOf("-s") + 1]);

        // The second pass reads what the first pass wrote.
        var firstOutput = calls[0].Arguments[calls[0].Arguments.ToList().IndexOf("-o") + 1];
        var secondInput = calls[1].Arguments[calls[1].Arguments.ToList().IndexOf("-i") + 1];
        Assert.Equal(firstOutput, secondInput);
    }

    [Fact]
    public async Task An_unsupported_scale_fails_clearly()
    {
        var input = _temp.CreateFile("a.png");
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }, options: ("scale", "3")));

        Assert.False(result.IsSuccess);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task The_general_model_is_the_default()
    {
        var input = _temp.CreateFile("a.png");
        await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }));

        var call = Assert.Single(_runner.RequestsFor("realesrgan"));
        Assert.Equal("realesr-general-x4v3", call.Arguments[call.Arguments.ToList().IndexOf("-n") + 1]);
    }

    [Fact]
    public async Task Denoise_switches_to_the_paired_denoise_model()
    {
        var input = _temp.CreateFile("a.png");
        await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }, options: ("denoise", "true")));

        var call = Assert.Single(_runner.RequestsFor("realesrgan"));
        Assert.Equal("realesr-general-wdn-x4v3", call.Arguments[call.Arguments.ToList().IndexOf("-n") + 1]);
    }

    [Fact]
    public async Task Forcing_cpu_is_passed_straight_through_to_every_pass()
    {
        var input = _temp.CreateFile("a.png");
        await Run(
            new UpscaleEngine(_runner, Tools()),
            Spec("upscale.photo", new[] { input }, options: new[] { ("scale", "8"), ("forceCpu", "true") }));

        var calls = _runner.RequestsFor("realesrgan");
        Assert.Equal(2, calls.Count);
        Assert.All(calls, call => Assert.Contains("-g", call.Arguments));
    }

    // --- Sharpening --------------------------------------------------------------------

    [Fact]
    public async Task Sharpening_runs_imagemagick_after_the_upscale()
    {
        var input = _temp.CreateFile("a.png");
        var result = await Run(
            new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }, options: ("sharpen", "true")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(_runner.RequestsFor("realesrgan"));

        var sharpenCall = Assert.Single(_runner.RequestsFor("magick"));
        Assert.Contains("-unsharp", sharpenCall.Arguments);

        // The final output is what the sharpen pass wrote, not the raw upscale.
        Assert.Equal("a.png", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Without_sharpening_imagemagick_is_never_touched()
    {
        var input = _temp.CreateFile("a.png");
        await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }));

        Assert.Empty(_runner.RequestsFor("magick"));
    }

    [Fact]
    public async Task Sharpening_without_imagemagick_fails_before_the_upscale_ever_runs()
    {
        var input = _temp.CreateFile("a.png");
        var result = await Run(
            new UpscaleEngine(_runner, Tools(imageMagick: false)),
            Spec("upscale.photo", new[] { input }, options: ("sharpen", "true")));

        Assert.False(result.IsSuccess);
        Assert.Contains("ImageMagick", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    // --- Batches -----------------------------------------------------------------------

    [Fact]
    public async Task A_batch_upscales_every_file_independently()
    {
        var inputs = new[] { _temp.CreateFile("a.png"), _temp.CreateFile("b.jpg") };
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", inputs));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(2, result.OutputPaths.Count);
        Assert.Equal(new[] { "a.png", "b.jpg" }, result.OutputPaths.Select(Path.GetFileName));
    }

    // --- Failure and cancellation ----------------------------------------------------------

    [Fact]
    public async Task Without_realesrgan_the_job_fails_before_any_process_runs()
    {
        var input = _temp.CreateFile("a.png");
        var result = await Run(new UpscaleEngine(_runner, Tools(realEsrgan: false)), Spec("upscale.photo", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("Real-ESRGAN", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_missing_file_fails_before_any_tool_runs()
    {
        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { _temp.Combine("gone.png") }));

        Assert.False(result.IsSuccess);
        Assert.Contains("gone.png", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_tool_failure_is_reported_with_its_own_message()
    {
        var input = _temp.CreateFile("a.png");
        _runner.NextFailure = "a.png: vkCreateInstance failed";

        var result = await Run(new UpscaleEngine(_runner, Tools()), Spec("upscale.photo", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("vkCreateInstance", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_before_the_job_starts_throws_rather_than_running_anything()
    {
        var input = _temp.CreateFile("a.png");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            new UpscaleEngine(_runner, Tools()).RunAsync(
                Spec("upscale.photo", new[] { input }),
                new DelegateProgress<JobProgress>(_ => { }),
                cancellation.Token));
    }
}
