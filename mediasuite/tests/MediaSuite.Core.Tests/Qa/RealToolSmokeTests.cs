using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Settings;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

/// <summary>
/// Runs one representative job per registered engine through the real production
/// wiring — a real <see cref="ProcessRunner"/>, a real <see cref="ToolLocator"/>, the
/// real <see cref="EngineSetup.CreateDefaultRegistry"/> registry and a real
/// <see cref="JobQueueManager"/> — deliberately pointed at a folder with no bundled
/// tools in it and an emptied PATH.
/// </summary>
/// <remarks>
/// This is the one piece of automated QA this sandbox can actually verify end to end.
/// It cannot verify a single real conversion: this environment has no Windows machine
/// and none of the bundled binaries, and neither does the CI runner that checks this
/// repository, so every job below is expected to fail on a missing-tool check rather
/// than run anything. What it does confirm, through the real objects rather than the
/// fakes every other test in this suite uses, is the experience of a brand-new install
/// before any tool has been dropped into <c>tools/</c>: that an engine is actually
/// registered for every operation here, that its required tool (checked by the queue
/// up front for most engines, or internally per operation for PDF and Document) is
/// actually noticed missing, and that the failure comes back as an ordinary failed job
/// rather than an unhandled exception taking the rest of the queue down with it.
/// See <c>QA.md</c> at the repository root for the real conversion QA a person runs
/// once the bundled tools are actually in place.
/// </remarks>
public sealed class RealToolSmokeTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    public void Dispose() => _temp.Dispose();

    public static IEnumerable<object?[]> RepresentativeOperations()
    {
        yield return new object?[] { "image.convert", "sample.jpg", null };
        yield return new object?[] { "image.png-to-svg", "sample.png", null };
        yield return new object?[] { "video.convert", "sample.mp4", null };
        yield return new object?[] { "gif.mp4-to-gif", "sample.mp4", null };
        yield return new object?[] { "pdf.compress", "sample.pdf", null };
        // document.convert has no forced format (it is the open-ended converter), and
        // DocumentEngine resolves the requested output format before it ever checks for
        // Pandoc or LibreOffice — an unset format throws its own, unrelated error first,
        // so this is the one case that needs an explicit format to actually reach the
        // missing-tool check this test exists to verify.
        yield return new object?[] { "document.convert", "sample.docx", "pdf" };
        yield return new object?[] { "archive.convert", "sample.zip", null };
        yield return new object?[] { "upscale.photo", "sample.jpg", null };
    }

    [Theory]
    [MemberData(nameof(RepresentativeOperations))]
    public async Task Every_registered_engine_fails_cleanly_with_no_tools_installed(
        string operationId, string fileName, string? outputFormat)
    {
        var locator = new ToolLocator(new[] { _temp.Combine("no-tools") }, pathVariable: string.Empty);
        var engines = EngineSetup.CreateDefaultRegistry(new ProcessRunner(), locator);
        var settings = new AppSettings { DefaultOutputDirectory = _temp.Combine("out") };

        using var queue = new JobQueueManager(
            engines, new DiskTempWorkspaceFactory(_temp.Combine("work")), maxConcurrency: 1, locator);

        var launcher = new JobLauncher(queue, settings);
        var input = _temp.CreateFile(fileName);
        var feature = FeatureCatalog.FromOperationId(operationId);

        Assert.NotNull(feature);
        Assert.True(engines.SupportsOperation(operationId), $"No engine is registered for '{operationId}'.");

        var job = Assert.Single(launcher.Launch(feature!, new[] { input }, outputFormat, QualityPreset.Balanced));
        await queue.WaitForIdleAsync().WaitAsync(TimeSpan.FromSeconds(10));

        Assert.Equal(JobStatus.Failed, job.Status);
        Assert.False(string.IsNullOrWhiteSpace(job.ErrorMessage));
        Assert.Contains("not installed", job.ErrorMessage, StringComparison.OrdinalIgnoreCase);
    }
}
