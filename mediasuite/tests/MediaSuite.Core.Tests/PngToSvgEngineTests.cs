using MediaSuite.Core.Engines;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class PngToSvgEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly FakeProcessRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(bool potrace = true)
    {
        _temp.CreateFile("tools", "imagemagick", "magick.exe");

        if (potrace)
        {
            _temp.CreateFile("tools", "potrace", "potrace.exe");
        }

        return new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty);
    }

    private JobSpec Spec(IReadOnlyList<string> inputs, params (string Key, string Value)[] options) => new()
    {
        OperationId = PngToSvgEngine.OperationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out") },
        WorkingDirectory = _temp.Combine("work"),
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static Task<JobResult> Run(PngToSvgEngine engine, JobSpec spec) =>
        engine.RunAsync(spec, new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    [Fact]
    public async Task Tracing_runs_ImageMagick_then_Potrace_and_writes_an_svg()
    {
        var input = _temp.CreateFile("logo.png");
        var engine = new PngToSvgEngine(_runner, Tools());

        var result = await Run(engine, Spec(new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("logo.svg", Path.GetFileName(Assert.Single(result.OutputPaths)));

        Assert.Equal(2, _runner.Requests.Count);
        Assert.Contains("magick", _runner.Requests[0].FileName, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("potrace", _runner.Requests[1].FileName, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task The_bitmap_handed_to_Potrace_is_the_one_ImageMagick_just_made()
    {
        var input = _temp.CreateFile("logo.png");
        var engine = new PngToSvgEngine(_runner, Tools());

        await Run(engine, Spec(new[] { input }));

        var bitmap = _runner.Requests[0].Arguments[^1];
        Assert.EndsWith(".pbm", bitmap, StringComparison.Ordinal);
        Assert.Contains(bitmap, _runner.Requests[1].Arguments);
    }

    [Fact]
    public async Task Without_Potrace_the_job_says_so_instead_of_half_running()
    {
        var input = _temp.CreateFile("logo.png");
        var engine = new PngToSvgEngine(_runner, Tools(potrace: false));

        var result = await Run(engine, Spec(new[] { input }));

        Assert.Equal(JobStatus.Failed, result.Status);
        Assert.Contains("Potrace", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public void The_bitmap_step_flattens_transparency_and_thresholds()
    {
        var arguments = PngToSvgEngine.BuildBitmapArguments(
            Spec(new[] { "logo.png" }, ("threshold", "65")), "logo.png", "trace.pbm");

        Assert.Equal("logo.png", arguments[0]);
        Assert.Contains("-alpha", arguments);
        Assert.Contains("Gray", arguments);
        Assert.Contains("65%", arguments);
        Assert.Equal("trace.pbm", arguments[^1]);
    }

    [Fact]
    public void An_absurd_threshold_is_clamped_into_range()
    {
        var arguments = PngToSvgEngine.BuildBitmapArguments(
            Spec(new[] { "logo.png" }, ("threshold", "5000")), "logo.png", "trace.pbm");

        Assert.Contains("99%", arguments);
    }

    [Fact]
    public void The_trace_step_names_its_output_and_carries_the_tuning_options()
    {
        var arguments = PngToSvgEngine.BuildTraceArguments(
            Spec(new[] { "logo.png" }, ("despeckle", "8"), ("cornerThreshold", "0.6")),
            "trace.pbm",
            "logo.svg");

        Assert.Contains("--svg", arguments);
        Assert.Contains("logo.svg", arguments);
        Assert.Contains("8", arguments);
        Assert.Contains("0.6", arguments);
        Assert.Equal("trace.pbm", arguments[^1]);
    }

    [Fact]
    public void Inverting_is_opt_in()
    {
        Assert.DoesNotContain(
            "--invert",
            PngToSvgEngine.BuildTraceArguments(Spec(new[] { "logo.png" }), "trace.pbm", "logo.svg"));

        Assert.Contains(
            "--invert",
            PngToSvgEngine.BuildTraceArguments(Spec(new[] { "logo.png" }, ("invert", "true")), "trace.pbm", "logo.svg"));
    }
}
