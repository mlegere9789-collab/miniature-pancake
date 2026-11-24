using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class ArchiveEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly ArchiveFakeToolRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(bool sevenZip = true)
    {
        if (sevenZip)
        {
            _temp.CreateFile("tools", "7zip", "7z.exe");
        }

        return new ToolLocator(new[] { _temp.Combine("tools") }, pathVariable: string.Empty);
    }

    private JobSpec Spec(string operationId, IReadOnlyList<string> inputs, string? format = null) => new()
    {
        OperationId = operationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out"), Format = format },
        WorkingDirectory = _temp.Combine("work"),
    };

    private static Task<JobResult> Run(ArchiveEngine engine, JobSpec spec) =>
        engine.RunAsync(spec, new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    // --- What the engine claims ----------------------------------------------------------

    [Fact]
    public void The_engine_claims_archive_convert_and_nothing_else()
    {
        var engine = new ArchiveEngine(_runner, Tools());

        Assert.True(engine.CanHandle(Spec("archive.convert", new[] { "a.zip" })));
        Assert.False(engine.CanHandle(Spec("pdf.merge", new[] { "a.pdf" })));
    }

    [Fact]
    public void Sevenzip_is_required_up_front_unlike_the_pdf_and_document_modules()
    {
        // There is only one tool in this module, so — unlike PDF or Document, where
        // different operations need different tools — nothing is lost by requiring it.
        Assert.Equal(new[] { ExternalToolId.SevenZip }, new ArchiveEngine(_runner, Tools()).RequiredTools);
    }

    // --- Ordinary conversion ---------------------------------------------------------------

    [Fact]
    public async Task Converting_extracts_then_recreates_in_the_target_format()
    {
        var input = _temp.CreateFile("photos.zip");
        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "7z"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("photos.7z", Path.GetFileName(Assert.Single(result.OutputPaths)));

        var extractCall = Assert.Single(_runner.RequestsFor("7z"), r => r.Arguments[0] == "x");
        Assert.Equal(input, extractCall.Arguments[1]);

        var createCall = Assert.Single(_runner.CreateCalls);
        Assert.Equal("-t7z", createCall.Arguments[1]);
    }

    [Fact]
    public async Task Every_extracted_file_is_passed_to_the_new_archive()
    {
        _runner.ExtractedFileNames = new[] { "one.txt", "two.txt", "three.txt" };
        var input = _temp.CreateFile("bundle.tar");

        await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "zip"));

        var createCall = Assert.Single(_runner.CreateCalls);
        Assert.Contains(createCall.Arguments, a => a.EndsWith("one.txt", StringComparison.Ordinal));
        Assert.Contains(createCall.Arguments, a => a.EndsWith("two.txt", StringComparison.Ordinal));
        Assert.Contains(createCall.Arguments, a => a.EndsWith("three.txt", StringComparison.Ordinal));
    }

    [Fact]
    public async Task Rar_is_a_perfectly_good_source_even_though_it_can_never_be_a_target()
    {
        var input = _temp.CreateFile("backup.rar");
        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "zip"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("backup.zip", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task No_format_chosen_is_refused_rather_than_guessed_at()
    {
        var input = _temp.CreateFile("a.zip");
        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("format", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task An_archive_that_extracts_to_nothing_fails_rather_than_producing_an_empty_one()
    {
        _runner.ExtractedFileNames = Array.Empty<string>();
        var input = _temp.CreateFile("empty.zip");

        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "7z"));

        Assert.False(result.IsSuccess);
        Assert.Contains("extracted to nothing", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    // --- The GZIP two-step -------------------------------------------------------------

    [Fact]
    public async Task A_gzip_target_bundles_into_a_tar_first()
    {
        _runner.ExtractedFileNames = new[] { "one.txt", "two.txt" };
        var input = _temp.CreateFile("photos.zip");

        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "gz"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("photos.gz", Path.GetFileName(Assert.Single(result.OutputPaths)));

        // Extract, then bundle into a TAR, then gzip that TAR — three 7-Zip calls, not two.
        var createCalls = _runner.CreateCalls;
        Assert.Equal(2, createCalls.Count);
        Assert.Equal("-ttar", createCalls[0].Arguments[1]);
        Assert.Equal("-tgzip", createCalls[1].Arguments[1]);

        // The second create call's only source is the TAR the first one just wrote.
        var tarPath = createCalls[0].Arguments[2];
        Assert.Equal(new[] { tarPath }, createCalls[1].Arguments.Skip(3).Take(1));
    }

    [Fact]
    public async Task A_gzip_target_still_bundles_a_single_extracted_file_into_a_tar()
    {
        // Uniform behaviour regardless of what the source archive happened to contain —
        // no special case for "exactly one file" that would make the output shape depend
        // on the input.
        _runner.ExtractedFileNames = new[] { "only.txt" };
        var input = _temp.CreateFile("solo.zip");

        await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "gz"));

        var createCalls = _runner.CreateCalls;
        Assert.Equal(2, createCalls.Count);
        Assert.Equal("-ttar", createCalls[0].Arguments[1]);
    }

    // --- Batches -----------------------------------------------------------------------

    [Fact]
    public async Task A_batch_converts_every_file_independently()
    {
        var inputs = new[] { _temp.CreateFile("a.zip"), _temp.CreateFile("b.zip") };
        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", inputs, "7z"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(2, result.OutputPaths.Count);
        Assert.Equal(2, _runner.RequestsFor("7z").Count(r => r.Arguments[0] == "x"));
    }

    // --- Failure and cancellation ----------------------------------------------------------

    [Fact]
    public async Task Without_sevenzip_the_job_fails_before_any_process_runs()
    {
        var input = _temp.CreateFile("a.zip");
        var result = await Run(new ArchiveEngine(_runner, Tools(sevenZip: false)), Spec("archive.convert", new[] { input }, "zip"));

        Assert.False(result.IsSuccess);
        Assert.Contains("7-Zip", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_missing_file_fails_before_any_tool_runs()
    {
        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { _temp.Combine("gone.zip") }, "zip"));

        Assert.False(result.IsSuccess);
        Assert.Contains("gone.zip", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_tool_failure_is_reported_with_its_own_message()
    {
        var input = _temp.CreateFile("a.zip");
        _runner.NextFailure = "a.zip: Data Error";

        var result = await Run(new ArchiveEngine(_runner, Tools()), Spec("archive.convert", new[] { input }, "zip"));

        Assert.False(result.IsSuccess);
        Assert.Contains("Data Error", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_before_the_job_starts_throws_rather_than_running_anything()
    {
        var input = _temp.CreateFile("a.zip");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            new ArchiveEngine(_runner, Tools()).RunAsync(
                Spec("archive.convert", new[] { input }, "zip"),
                new DelegateProgress<JobProgress>(_ => { }),
                cancellation.Token));
    }
}
