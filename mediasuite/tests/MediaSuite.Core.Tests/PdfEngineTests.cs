using MediaSuite.Core.Engines;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class PdfEngineTests : IDisposable
{
    private readonly TempDirectory _temp = new();
    private readonly PdfFakeToolRunner _runner = new();

    public void Dispose() => _temp.Dispose();

    private ToolLocator Tools(
        bool qpdf = true, bool ghostscript = true, bool mutool = true,
        bool libreOffice = true, bool imageMagick = true)
    {
        if (qpdf)
        {
            _temp.CreateFile("tools", "qpdf", "qpdf.exe");
        }

        if (ghostscript)
        {
            _temp.CreateFile("tools", "ghostscript", "gswin64c.exe");
        }

        if (mutool)
        {
            _temp.CreateFile("tools", "mupdf", "mutool.exe");
        }

        if (libreOffice)
        {
            _temp.CreateFile("tools", "libreoffice", "soffice.exe");
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
        QualityPreset preset = QualityPreset.Balanced,
        params (string Key, string Value)[] options) => new()
    {
        OperationId = operationId,
        InputPaths = inputs,
        Output = new OutputTarget { Directory = _temp.Combine("out"), Format = format },
        Preset = preset,
        WorkingDirectory = _temp.Combine("work"),
        Options = options.ToDictionary(o => o.Key, o => o.Value, StringComparer.OrdinalIgnoreCase),
    };

    private static Task<JobResult> Run(PdfEngine engine, JobSpec spec) =>
        engine.RunAsync(spec, new DelegateProgress<JobProgress>(_ => { }), CancellationToken.None);

    // --- What the engine claims ----------------------------------------------------------

    [Fact]
    public void The_engine_claims_every_pdf_operation_and_nothing_else()
    {
        var engine = new PdfEngine(_runner, Tools());

        foreach (var operation in PdfOperations.All)
        {
            Assert.True(engine.CanHandle(Spec(operation, new[] { "a.pdf" })), operation);
        }

        Assert.False(engine.CanHandle(Spec("video.convert", new[] { "a.mp4" })));
        Assert.False(engine.CanHandle(Spec("image.convert", new[] { "a.png" })));
    }

    [Fact]
    public void No_single_tool_is_demanded_up_front()
    {
        // Merge only needs QPDF; requiring Ghostscript or MuPDF too would block it on a
        // machine that only has QPDF installed.
        Assert.Empty(new PdfEngine(_runner, Tools()).RequiredTools);
    }

    // --- Merge -----------------------------------------------------------------------------

    [Fact]
    public async Task Merging_is_one_job_over_every_file_in_order()
    {
        var inputs = new[] { _temp.CreateFile("a.pdf"), _temp.CreateFile("b.pdf"), _temp.CreateFile("c.pdf") };
        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.merge", inputs));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        var output = Assert.Single(result.OutputPaths);
        Assert.Equal("a.pdf", Path.GetFileName(output));

        var call = Assert.Single(_runner.RequestsFor("qpdf"));
        var args = call.Arguments.ToList();
        Assert.True(args.IndexOf(inputs[0]) < args.IndexOf(inputs[1]));
        Assert.True(args.IndexOf(inputs[1]) < args.IndexOf(inputs[2]));
    }

    [Fact]
    public async Task Merging_without_qpdf_fails_with_a_readable_message()
    {
        var inputs = new[] { _temp.CreateFile("a.pdf"), _temp.CreateFile("b.pdf") };
        var result = await Run(new PdfEngine(_runner, Tools(qpdf: false)), Spec("pdf.merge", inputs));

        Assert.False(result.IsSuccess);
        Assert.Contains("QPDF", result.ErrorMessage!, StringComparison.Ordinal);
    }

    // --- Split -------------------------------------------------------------------------

    [Fact]
    public async Task Splitting_every_n_pages_produces_one_file_per_group()
    {
        _runner.PageCount = 5;
        var input = _temp.CreateFile("book.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.split", new[] { input }, options: ("every", "2")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(3, result.OutputPaths.Count); // 2 + 2 + 1
        Assert.All(result.OutputPaths, path => Assert.True(File.Exists(path)));
    }

    [Fact]
    public async Task Splitting_defaults_to_one_page_per_file()
    {
        _runner.PageCount = 4;
        var input = _temp.CreateFile("book.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.split", new[] { input }));

        Assert.Equal(4, result.OutputPaths.Count);
    }

    [Fact]
    public async Task Splitting_by_explicit_ranges_writes_one_call_per_range()
    {
        var input = _temp.CreateFile("book.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.split", new[] { input }, options: ("ranges", "1-3;4-6;7")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(3, result.OutputPaths.Count);

        var calls = _runner.RequestsFor("qpdf");
        Assert.Contains(calls, c => c.Arguments.Contains("1-3"));
        Assert.Contains(calls, c => c.Arguments.Contains("4-6"));
        Assert.Contains(calls, c => c.Arguments.Contains("7"));
    }

    // --- Rotate / unlock / protect / flatten ---------------------------------------------

    [Fact]
    public async Task Rotating_defaults_to_ninety_degrees()
    {
        var input = _temp.CreateFile("in.pdf");
        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.rotate", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Contains(_runner.RequestsFor("qpdf")[0].Arguments, a => a == "--rotate=90");
    }

    [Fact]
    public async Task Protecting_requires_a_password()
    {
        var input = _temp.CreateFile("in.pdf");
        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.protect", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("password", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Protecting_with_a_password_succeeds()
    {
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.protect", new[] { input }, options: ("password", "hunter2")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
    }

    [Fact]
    public async Task Unlocking_and_flattening_both_run_a_single_qpdf_call()
    {
        var input = _temp.CreateFile("in.pdf");
        var engine = new PdfEngine(_runner, Tools());

        await Run(engine, Spec("pdf.unlock", new[] { input }, options: ("password", "x")));
        await Run(engine, Spec("pdf.flatten", new[] { input }));

        Assert.Equal(2, _runner.RequestsFor("qpdf").Count);
    }

    // --- Organize / extract-pages / remove-pages ------------------------------------------

    [Fact]
    public async Task Organizing_needs_a_page_list()
    {
        var input = _temp.CreateFile("in.pdf");
        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.organize", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("pages", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Organizing_passes_the_page_list_straight_through()
    {
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.organize", new[] { input }, options: ("pages", "3,1,2,2")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Contains(_runner.RequestsFor("qpdf")[0].Arguments, a => a == "3,1,2,2");
    }

    [Fact]
    public async Task Removing_pages_probes_the_page_count_first()
    {
        _runner.PageCount = 5;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.remove-pages", new[] { input }, options: ("remove", "2,4")));

        Assert.True(result.IsSuccess, result.ErrorMessage);

        var calls = _runner.RequestsFor("qpdf");
        Assert.Contains(calls, c => c.Arguments.Contains("--show-npages"));

        var selectCall = calls.First(c => c.Arguments.Contains("--pages"));
        var pageListIndex = selectCall.Arguments.ToList().IndexOf(".") + 1;
        Assert.Equal("1,3,5", selectCall.Arguments[pageListIndex]);
    }

    [Fact]
    public async Task Removing_pages_understands_qpdfs_own_from_the_end_syntax()
    {
        // "r1" (qpdf's own "last page, counting from the end" syntax) already works in
        // organize, extract-pages, rotate and split-ranges, since those hand the user's
        // text straight through to qpdf's own --pages option. "remove" is the one feature
        // that pre-resolves its page list itself, so without explicit support for "rN" it
        // would reject the exact same token every other PDF page-selection field accepts.
        _runner.PageCount = 5;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.remove-pages", new[] { input }, options: ("remove", "r1")));

        Assert.True(result.IsSuccess, result.ErrorMessage);

        var selectCall = _runner.RequestsFor("qpdf").First(c => c.Arguments.Contains("--pages"));
        var pageListIndex = selectCall.Arguments.ToList().IndexOf(".") + 1;
        Assert.Equal("1,2,3,4", selectCall.Arguments[pageListIndex]);
    }

    [Fact]
    public async Task Removing_every_page_is_refused_rather_than_producing_an_empty_pdf()
    {
        _runner.PageCount = 2;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.remove-pages", new[] { input }, options: ("remove", "1-2")));

        Assert.False(result.IsSuccess);
        Assert.Contains("empty", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Extracting_pages_keeps_only_the_ones_named()
    {
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(
            new PdfEngine(_runner, Tools()),
            Spec("pdf.extract-pages", new[] { input }, options: ("pages", "1-2")));

        Assert.True(result.IsSuccess, result.ErrorMessage);
    }

    // --- Crop / resize / compress --------------------------------------------------------

    [Fact]
    public async Task Cropping_and_resizing_and_compressing_all_run_through_ghostscript()
    {
        var input = _temp.CreateFile("in.pdf");
        var engine = new PdfEngine(_runner, Tools());

        await Run(engine, Spec("pdf.crop", new[] { input }));
        await Run(engine, Spec("pdf.resize", new[] { input }, options: ("paperSize", "letter")));
        await Run(engine, Spec("pdf.compress", new[] { input }, preset: QualityPreset.Quick));

        Assert.Equal(3, _runner.RequestsFor("gswin64c").Count);
    }

    [Fact]
    public async Task Compressing_without_an_explicit_setting_follows_the_preset()
    {
        var input = _temp.CreateFile("in.pdf");

        await Run(new PdfEngine(_runner, Tools()), Spec("pdf.compress", new[] { input }, preset: QualityPreset.Best));

        Assert.Contains(_runner.RequestsFor("gswin64c")[0].Arguments, a => a == "-dPDFSETTINGS=/prepress");
    }

    [Fact]
    public async Task Resizing_without_a_ghostscript_binary_fails_clearly()
    {
        var input = _temp.CreateFile("in.pdf");
        var result = await Run(new PdfEngine(_runner, Tools(ghostscript: false)), Spec("pdf.resize", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("Ghostscript", result.ErrorMessage!, StringComparison.Ordinal);
    }

    // --- PDF to JPG, PDF to Word, extract images -------------------------------------------

    [Fact]
    public async Task Rendering_to_jpg_produces_one_file_per_page()
    {
        _runner.PageCount = 4;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.to-jpg", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(4, result.OutputPaths.Count);
        Assert.All(result.OutputPaths, path => Assert.Equal("jpg", Path.GetExtension(path).TrimStart('.').ToLowerInvariant()));
    }

    [Fact]
    public async Task Converting_to_word_asks_libreoffice_and_keeps_the_docx_it_writes()
    {
        var input = _temp.CreateFile("report.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.to-word", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal("report.docx", Path.GetFileName(Assert.Single(result.OutputPaths)));

        var call = Assert.Single(_runner.RequestsFor("soffice"));
        Assert.Contains("--headless", call.Arguments);
        Assert.Contains("docx", call.Arguments);
    }

    [Fact]
    public async Task Extracting_images_returns_every_image_the_pdf_contained()
    {
        _runner.ExtractedImageCount = 3;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.extract-images", new[] { input }));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(3, result.OutputPaths.Count);
    }

    [Fact]
    public async Task Extracting_images_from_a_pdf_with_none_fails_rather_than_succeeding_empty()
    {
        _runner.ExtractedImageCount = 0;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.extract-images", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("no embedded images", result.ErrorMessage!, StringComparison.OrdinalIgnoreCase);
    }

    // --- Images to PDF and HEIC to PDF -----------------------------------------------------

    [Fact]
    public async Task Jpg_to_pdf_combines_every_staged_image_into_one_file()
    {
        var inputs = new[] { _temp.CreateFile("1.jpg"), _temp.CreateFile("2.jpg") };

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("image.jpg-to-pdf", inputs));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        var output = Assert.Single(result.OutputPaths);
        Assert.Equal(".pdf", Path.GetExtension(output));

        var call = Assert.Single(_runner.RequestsFor("mutool"));
        Assert.Equal("convert", call.Arguments[0]);
    }

    [Fact]
    public async Task Heic_to_pdf_converts_each_photo_through_imagemagick_before_assembling()
    {
        var inputs = new[] { _temp.CreateFile("1.heic"), _temp.CreateFile("2.heic") };

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("image.heic-to-pdf", inputs));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(result.OutputPaths);

        // Two JPEG conversions, one for each photo, before the single mutool assembly.
        Assert.Equal(2, _runner.RequestsFor("magick").Count);
        var assembly = Assert.Single(_runner.RequestsFor("mutool"));
        Assert.All(assembly.Arguments.Skip(3), argument => Assert.EndsWith(".jpg", argument, StringComparison.Ordinal));
    }

    [Fact]
    public async Task Heic_to_pdf_without_imagemagick_fails_before_touching_mutool()
    {
        var inputs = new[] { _temp.CreateFile("1.heic") };

        var result = await Run(new PdfEngine(_runner, Tools(imageMagick: false)), Spec("image.heic-to-pdf", inputs));

        Assert.False(result.IsSuccess);
        Assert.Empty(_runner.RequestsFor("mutool"));
    }

    // --- pdf.convert, both directions -------------------------------------------------------

    [Fact]
    public async Task Converting_a_pdf_to_an_image_format_renders_its_pages()
    {
        _runner.PageCount = 2;
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.convert", new[] { input }, format: "png"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(2, result.OutputPaths.Count);
        Assert.All(result.OutputPaths, path => Assert.Equal("png", Path.GetExtension(path).TrimStart('.').ToLowerInvariant()));
    }

    [Fact]
    public async Task Converting_an_image_to_pdf_assembles_it_as_a_single_page()
    {
        var input = _temp.CreateFile("photo.png");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.convert", new[] { input }, format: "pdf"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(".pdf", Path.GetExtension(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task An_image_in_a_mixed_batch_is_still_named_pdf_even_when_the_picker_chose_jpg()
    {
        // A batch can mix PDFs and images under one pdf.convert job with a single format
        // choice for the whole batch. If that choice is "jpg" (meant for the PDF pages),
        // an image input still has to be assembled into a real PDF and must not come out
        // named "photo.jpg" while actually containing a PDF.
        var input = _temp.CreateFile("photo.png");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.convert", new[] { input }, format: "jpg"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Equal(".pdf", Path.GetExtension(Assert.Single(result.OutputPaths)));
    }

    [Fact]
    public async Task Converting_a_pdf_to_pdf_rewrites_it_through_qpdf()
    {
        var input = _temp.CreateFile("in.pdf");

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.convert", new[] { input }, format: "pdf"));

        Assert.True(result.IsSuccess, result.ErrorMessage);
        Assert.Single(_runner.RequestsFor("qpdf"));
    }

    // --- Failure and cancellation ----------------------------------------------------------

    [Fact]
    public async Task A_missing_file_fails_before_any_tool_runs()
    {
        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.compress", new[] { _temp.Combine("gone.pdf") }));

        Assert.False(result.IsSuccess);
        Assert.Contains("gone.pdf", result.ErrorMessage!, StringComparison.Ordinal);
        Assert.Empty(_runner.Requests);
    }

    [Fact]
    public async Task A_tool_failure_is_reported_with_its_own_message()
    {
        var input = _temp.CreateFile("in.pdf");
        _runner.NextFailure = "in.pdf: file is damaged";

        var result = await Run(new PdfEngine(_runner, Tools()), Spec("pdf.compress", new[] { input }));

        Assert.False(result.IsSuccess);
        Assert.Contains("damaged", result.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Cancelling_before_the_job_starts_throws_rather_than_running_anything()
    {
        var input = _temp.CreateFile("in.pdf");
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(() =>
            new PdfEngine(_runner, Tools()).RunAsync(
                Spec("pdf.compress", new[] { input }),
                new DelegateProgress<JobProgress>(_ => { }),
                cancellation.Token));
    }
}
