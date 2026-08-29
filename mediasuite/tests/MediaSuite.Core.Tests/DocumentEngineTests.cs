using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class DocumentEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly DocumentFakeToolRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(bool pandoc = true, bool libreOffice = true, bool calibre = true)
    {
        if (pandoc)
        {
            _temp.CreateFile("tools", "pandoc", "pandoc.exe");
        }

        if (libreOffice)
        {
            _temp.CreateFile("tools", "libreoffice", "soffice.exe");
        }

        if (calibre)
        {
            _temp.CreateFile("tools", "calibre", "ebook-convert.exe");
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

    private static Task<JobResult> Run(DocumentEngine engine, JobSpec spec) =>
        engine.RunAsync(spec, new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    // --- What the engine claims ----------------------------------------------------------

    [Fact]
    public void The_engine_claims_every_document_and_ebook_operation_and_nothing_else()
    {
        var engine = new DocumentEngine(_runner, Tools());

        foreach (var operation in DocumentOperations.All)
        {
            Assert.True(engine.CanHandle(Spec(operation, new[] { "a.docx" })), operation);
        }

        Assert.False(engine.CanHandle(Spec("pdf.merge", new[] { "a.pdf" })));
        Assert.False(engine.CanHandle(Spec("video.convert", new[] { "a.mp4" })));
    }

    [Fact]
    public void No_single_tool_is_demanded_up_front()
    {
        Assert.Empty(new DocumentEngine(_runner, Tools()).RequiredTools);
    }

    // --- document.convert --------------------------------------------------------------

    [Fact]
    public async Task Converting_between_modern_formats_goes_through_pandoc()
    {
        var input = _temp.CreateFile("notes.md");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }, "docx"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("notes.docx", Path.GetFileName(Assert.Single(result.OutputPaths)));

        var call = Assert.Single(_runner.RequestsFor("pandoc"));
        Assert.Contains("markdown", call.Arguments);
        Assert.Contains("docx", call.Arguments);
    }

    [Fact]
    public async Task Converting_the_legacy_doc_format_goes_through_libreoffice_instead()
    {
        var input = _temp.CreateFile("old.doc");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }, "odt"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("old.odt", Path.GetFileName(Assert.Single(result.OutputPaths)));

        Assert.Single(_runner.RequestsFor("soffice"));
        Assert.Empty(_runner.RequestsFor("pandoc"));
    }

    [Fact]
    public async Task Converting_to_the_legacy_doc_format_also_goes_through_libreoffice()
    {
        var input = _temp.CreateFile("notes.md");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }, "doc"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(_runner.RequestsFor("soffice"));
    }

    [Fact]
    public async Task No_format_chosen_is_refused_rather_than_guessed_at()
    {
        var input = _temp.CreateFile("notes.md");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("format", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Without_pandoc_an_ordinary_conversion_fails_clearly()
    {
        var input = _temp.CreateFile("notes.md");
        var result = await Run(new DocumentEngine(_runner, Tools(pandoc: false)), Spec("document.convert", new[] { input }, "docx"));

        Assert.False(result.IsSuccess);
        Assert.Contains("Pandoc", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_batch_converts_every_file_and_keeps_going_through_the_batch()
    {
        var inputs = new[] { _temp.CreateFile("one.md"), _temp.CreateFile("two.md") };
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", inputs, "html"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(2, result.OutputPaths.Count);
        Assert.Equal(2, _runner.RequestsFor("pandoc").Count);
    }

    // --- document.docx-to-pdf -------------------------------------------------------------

    [Fact]
    public async Task Docx_to_pdf_always_uses_libreoffice_never_pandoc()
    {
        var input = _temp.CreateFile("report.docx");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.docx-to-pdf", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("report.pdf", Path.GetFileName(Assert.Single(result.OutputPaths)));
        Assert.Single(_runner.RequestsFor("soffice"));
        Assert.Empty(_runner.RequestsFor("pandoc"));
    }

    [Fact]
    public async Task Docx_to_pdf_ignores_any_format_the_picker_might_have_left_selected()
    {
        // The tool is named for one direction; a stale picker value must not let a DOCX
        // job come out named ".docx" instead of ".pdf".
        var input = _temp.CreateFile("report.docx");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.docx-to-pdf", new[] { input }, "docx"));

        Assert.Equal("report.pdf", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    // --- Ebooks, and the PDF bridges --------------------------------------------------------

    [Fact]
    public async Task Ebook_conversion_goes_through_calibre()
    {
        var input = _temp.CreateFile("book.epub");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("ebook.convert", new[] { input }, "mobi"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("book.mobi", Path.GetFileName(Assert.Single(result.OutputPaths)));
        Assert.Single(_runner.RequestsFor("ebook-convert"));
    }

    [Fact]
    public async Task Pdf_to_epub_is_fixed_to_epub_regardless_of_any_chosen_format()
    {
        var input = _temp.CreateFile("book.pdf");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("pdf.to-epub", new[] { input }, "mobi"));

        Assert.Equal("book.epub", Path.GetFileName(Assert.Single(result.OutputPaths)));
        Assert.Single(_runner.RequestsFor("ebook-convert"));
    }

    [Fact]
    public async Task Epub_to_pdf_is_fixed_to_pdf()
    {
        var input = _temp.CreateFile("book.epub");
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("ebook.epub-to-pdf", new[] { input }));

        Assert.Equal("book.pdf", Path.GetFileName(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Without_calibre_an_ebook_job_fails_clearly()
    {
        var input = _temp.CreateFile("book.epub");
        var result = await Run(new DocumentEngine(_runner, Tools(calibre: false)), Spec("ebook.convert", new[] { input }, "mobi"));

        Assert.False(result.IsSuccess);
        Assert.Contains("Calibre", result.ErrorMessage!, StringComparison.Ordinal);
    }

    // --- Failure and cancellation ----------------------------------------------------------

    [Fact]
    public async Task A_missing_file_fails_before_any_tool_runs()
    {
        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { _temp.Combine("gone.md") }, "docx"));

        Assert.False(result.IsSuccess);
        Assert.Contains("gone.md", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_tool_failure_is_reported_with_its_own_message()
    {
        var input = _temp.CreateFile("notes.md");
        _runner.NextFailure = "notes.md: unknown reader";

        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }, "docx"));

        Assert.False(result.IsSuccess);
        Assert.Contains("unknown reader", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_libreoffice_success_that_writes_nothing_is_still_reported_as_a_failure()
    {
        // LibreOffice's own exit code can be 0 even when its conversion silently produced
        // nothing — a missing import filter is one real way this happens — so the engine
        // has to check the file actually landed, not just trust the exit code.
        var input = _temp.CreateFile("old.doc");
        _runner.NextLibreOfficeWritesNothing = true;

        var result = await Run(new DocumentEngine(_runner, Tools()), Spec("document.convert", new[] { input }, "odt"));

        Assert.False(result.IsSuccess);
        Assert.Contains("wrote nothing", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Cancelling_before_the_job_starts_throws_rather_than_running_anything()
    {
        var input = _temp.CreateFile("notes.md");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            new DocumentEngine(_runner, Tools()).RunAsync(
                Spec("document.convert", new[] { input }, "docx"),
                new DelegateProgress<JobProgress>(_ => { }),
                cancellation.Token));
    }
}
