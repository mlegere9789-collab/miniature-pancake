using System.Globalization;
using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The PDF module: merge, split, compress, rotate, protect, crop, resize, flatten,
/// organise pages, extract embedded images, and convert to and from JPG, Word and images —
/// driven by QPDF, Ghostscript, MuPDF and, for PDF to Word, LibreOffice.
/// </summary>
/// <remarks>
/// Unlike the FFmpeg and GIF engines, no single binary is required for the whole module —
/// merge only needs QPDF, resize only needs Ghostscript — so each operation asks for its
/// own tool rather than the engine demanding all four up front.
/// </remarks>
public sealed class PdfEngine : ExternalProcessEngine
{
    public PdfEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator)
    {
    }

    public override string Id => "pdf";

    public override string DisplayName => "PDF (QPDF, Ghostscript, MuPDF)";

    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = Array.Empty<ExternalToolId>();

    public override bool CanHandle(JobSpec spec) => PdfOperations.All.Contains(spec.OperationId);

    public override async Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(progress);

        if (spec.InputPaths.Count == 0)
        {
            return JobResult.Failure("No input files were given.");
        }

        foreach (var path in spec.InputPaths)
        {
            if (!File.Exists(path))
            {
                return JobResult.Failure($"'{Path.GetFileName(path)}' no longer exists.");
            }
        }

        var workingDirectory = ResolveWorkingDirectory(spec);
        var operation = spec.OperationId.ToLowerInvariant();

        try
        {
            var outputs = OperationInputRules.CombinesInputs(operation)
                ? await CombineAsync(spec, operation, workingDirectory, progress, cancellationToken).ConfigureAwait(false)
                : await ConvertEachAsync(spec, operation, workingDirectory, progress, cancellationToken).ConfigureAwait(false);

            progress.Report(JobProgress.At(100, "Done"));
            return JobResult.Success(outputs, TimeSpan.Zero);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return JobResult.Failure(ex.Message);
        }
    }

    // --- Combining several inputs into one output ---------------------------------------

    private async Task<List<string>> CombineAsync(
        JobSpec spec,
        string operation,
        string workingDirectory,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        progress.Report(new JobProgress(10, "Combining", $"{spec.InputPaths.Count} files"));

        var outputPath = ResolveOutputPath(spec, spec.InputPaths[0], 1, batchRoot: null);

        if (operation == "pdf.merge")
        {
            var qpdf = RequireTool(ExternalToolId.QPdf);
            await RunToolAsync(
                qpdf, PdfCommandBuilder.Merge(spec.InputPaths, outputPath), "QPDF", cancellationToken,
                outputPathToDeleteOnCancel: outputPath)
                .ConfigureAwait(false);
        }
        else
        {
            var imagePaths = operation == "image.heic-to-pdf"
                ? await ConvertHeicBatchToJpegAsync(spec, workingDirectory, cancellationToken).ConfigureAwait(false)
                : spec.InputPaths;

            var mutool = RequireTool(ExternalToolId.MuPdf);
            await RunToolAsync(
                mutool, PdfCommandBuilder.ImagesToPdf(imagePaths, outputPath), "MuPDF", cancellationToken,
                outputPathToDeleteOnCancel: outputPath)
                .ConfigureAwait(false);
        }

        RequireOutput(outputPath, "the combined PDF");
        return new List<string> { outputPath };
    }

    /// <summary>
    /// mutool has no HEIC decoder, so each photo goes through ImageMagick first — the same
    /// engine that already turns HEIC into JPEG for the image module.
    /// </summary>
    private async Task<List<string>> ConvertHeicBatchToJpegAsync(
        JobSpec spec,
        string workingDirectory,
        CancellationToken cancellationToken)
    {
        var magick = RequireTool(ExternalToolId.ImageMagick);
        var jpegPaths = new List<string>(spec.InputPaths.Count);

        for (var index = 0; index < spec.InputPaths.Count; index++)
        {
            var inputPath = spec.InputPaths[index];
            var jpegPath = Path.Combine(
                workingDirectory, $"heic-{index.ToString(CultureInfo.InvariantCulture)}.jpg");

            var convertSpec = spec with
            {
                OperationId = "image.convert",
                InputPaths = new[] { inputPath },
                Output = new OutputTarget { Directory = workingDirectory, Format = "jpg" },
            };

            var arguments = ImageCommandBuilder.Build(convertSpec, inputPath, jpegPath);
            await RunToolAsync(magick, arguments, "ImageMagick", cancellationToken).ConfigureAwait(false);

            RequireOutput(jpegPath, Path.GetFileName(inputPath));
            jpegPaths.Add(jpegPath);
        }

        return jpegPaths;
    }

    // --- One input at a time --------------------------------------------------------------

    private async Task<List<string>> ConvertEachAsync(
        JobSpec spec,
        string operation,
        string workingDirectory,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        var batchRoot = spec.Output.PreserveFolderStructure
            ? OutputPathResolver.FindCommonRoot(spec.InputPaths)
            : null;

        var outputs = new List<string>();
        var total = spec.InputPaths.Count;

        for (var index = 0; index < total; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];
            progress.Report(new JobProgress(index * 100d / total, StageFor(operation), Path.GetFileName(inputPath)));

            var stepFolder = Path.Combine(workingDirectory, index.ToString(CultureInfo.InvariantCulture));
            Directory.CreateDirectory(stepFolder);

            var produced = await RunOneAsync(
                spec, operation, inputPath, index + 1, batchRoot, stepFolder, cancellationToken)
                .ConfigureAwait(false);

            outputs.AddRange(produced);
        }

        return outputs;
    }

    private async Task<IReadOnlyList<string>> RunOneAsync(
        JobSpec spec,
        string operation,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        switch (operation)
        {
            case "pdf.split":
                return await SplitAsync(spec, inputPath, batchRoot, stepFolder, cancellationToken).ConfigureAwait(false);

            case "pdf.extract-images":
                return await ExtractImagesAsync(spec, inputPath, index, batchRoot, stepFolder, cancellationToken)
                    .ConfigureAwait(false);

            case "pdf.to-jpg":
                return await RenderPagesAsync(spec, inputPath, index, batchRoot, stepFolder, "jpg", cancellationToken)
                    .ConfigureAwait(false);

            case "pdf.to-word":
                return new[]
                {
                    await ConvertToWordAsync(spec, inputPath, index, batchRoot, stepFolder, cancellationToken)
                        .ConfigureAwait(false),
                };

            case "pdf.remove-pages":
                return new[]
                {
                    await RemovePagesAsync(spec, inputPath, index, batchRoot, cancellationToken).ConfigureAwait(false),
                };

            case "pdf.convert":
                return await ConvertPdfOrImageAsync(spec, inputPath, index, batchRoot, stepFolder, cancellationToken)
                    .ConfigureAwait(false);

            default:
                return new[]
                {
                    await RunSingleOutputAsync(spec, operation, inputPath, index, batchRoot, cancellationToken)
                        .ConfigureAwait(false),
                };
        }
    }

    /// <summary>The operations that produce exactly one PDF from one PDF via a single call.</summary>
    private async Task<string> RunSingleOutputAsync(
        JobSpec spec,
        string operation,
        string inputPath,
        int index,
        string? batchRoot,
        CancellationToken cancellationToken)
    {
        var outputPath = ResolveOutputPath(spec, inputPath, index, batchRoot);

        // The tool each of these needs comes from PdfCommandBuilder.ToolFor rather than
        // being repeated here, so there is exactly one place that maps an operation to a
        // binary — a second copy could quietly drift out of step with it.
        IReadOnlyList<string> arguments = operation switch
        {
            "pdf.rotate" => PdfCommandBuilder.Rotate(
                inputPath, outputPath, ResolveRotationDegrees(spec), spec.GetOption("pages")),
            "pdf.unlock" => PdfCommandBuilder.Unlock(inputPath, outputPath, spec.GetOption("password", string.Empty)),
            "pdf.protect" => PdfCommandBuilder.Protect(
                inputPath,
                outputPath,
                RequirePassword(spec),
                spec.GetOption("ownerPassword"),
                spec.GetBool("allowPrinting", true),
                spec.GetBool("allowCopying", true)),
            "pdf.flatten" => PdfCommandBuilder.Flatten(inputPath, outputPath),
            "pdf.organize" or "pdf.extract-pages" => PdfCommandBuilder.SelectPages(
                inputPath, outputPath, RequirePageList(spec, "pages")),
            "pdf.crop" => PdfCommandBuilder.Crop(inputPath, outputPath, spec.GetDouble("marginPoints", 36)),
            "pdf.resize" => PdfCommandBuilder.Resize(
                inputPath, outputPath, ResolvePaperWidth(spec), ResolvePaperHeight(spec)),
            "pdf.compress" => PdfCommandBuilder.Compress(inputPath, outputPath, ResolvePdfSettings(spec)),
            _ => throw new ArgumentException($"'{operation}' is not a single-pass PDF operation.", nameof(operation)),
        };

        var toolId = PdfCommandBuilder.ToolFor(operation);
        var tool = RequireTool(toolId);
        await RunToolAsync(
            tool, arguments, ToolManifest.Get(toolId).DisplayName, cancellationToken, outputPathToDeleteOnCancel: outputPath)
            .ConfigureAwait(false);
        RequireOutput(outputPath, Path.GetFileName(inputPath));
        return outputPath;
    }

    // --- Operations needing more than one process call ------------------------------------

    private async Task<string> RemovePagesAsync(
        JobSpec spec,
        string inputPath,
        int index,
        string? batchRoot,
        CancellationToken cancellationToken)
    {
        var qpdf = RequireTool(ExternalToolId.QPdf);
        var totalPages = await CountPagesAsync(qpdf, inputPath, cancellationToken).ConfigureAwait(false);
        var removed = ParsePageNumbers(RequirePageList(spec, "remove"), totalPages);

        var kept = Enumerable.Range(1, totalPages).Where(page => !removed.Contains(page)).ToList();

        if (kept.Count == 0)
        {
            throw new ToolExecutionException(
                $"'{Path.GetFileName(inputPath)}': every page was removed, which would leave an empty PDF.");
        }

        var outputPath = ResolveOutputPath(spec, inputPath, index, batchRoot);
        var pageList = string.Join(",", kept.Select(page => page.ToString(CultureInfo.InvariantCulture)));

        await RunToolAsync(
            qpdf, PdfCommandBuilder.SelectPages(inputPath, outputPath, pageList), "QPDF", cancellationToken,
            outputPathToDeleteOnCancel: outputPath)
            .ConfigureAwait(false);

        RequireOutput(outputPath, Path.GetFileName(inputPath));
        return outputPath;
    }

    private async Task<int> CountPagesAsync(string qpdf, string inputPath, CancellationToken cancellationToken)
    {
        var result = await ProcessRunner.RunAsync(
            new ProcessRequest { FileName = qpdf, Arguments = PdfCommandBuilder.CountPages(inputPath) },
            cancellationToken).ConfigureAwait(false);

        if (!result.IsSuccess || !int.TryParse(result.StandardOutput.Trim(), out var pages) || pages <= 0)
        {
            throw new ToolExecutionException(
                $"QPDF could not read the page count of '{Path.GetFileName(inputPath)}': {result.DescribeFailure()}",
                result.FullOutput());
        }

        return pages;
    }

    private async Task<IReadOnlyList<string>> SplitAsync(
        JobSpec spec,
        string inputPath,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var qpdf = RequireTool(ExternalToolId.QPdf);
        var ranges = spec.GetOption("ranges");

        if (!string.IsNullOrWhiteSpace(ranges))
        {
            var parts = new List<string>();

            foreach (var group in ranges.Split(';', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
            {
                var partPath = Path.Combine(stepFolder, $"part-{parts.Count + 1}.pdf");
                await RunToolAsync(
                    qpdf, PdfCommandBuilder.SelectPages(inputPath, partPath, group), "QPDF", cancellationToken)
                    .ConfigureAwait(false);
                RequireOutput(partPath, Path.GetFileName(inputPath));
                parts.Add(partPath);
            }

            return MoveEach(parts, spec, inputPath, batchRoot);
        }

        var pattern = Path.Combine(stepFolder, "part-%d.pdf");
        await RunToolAsync(
            qpdf,
            PdfCommandBuilder.SplitEvery(inputPath, pattern, Math.Max(1, spec.GetInt("every", 1))),
            "QPDF",
            cancellationToken).ConfigureAwait(false);

        var produced = NaturalSort(Directory.GetFiles(stepFolder, "part-*.pdf"));

        if (produced.Count == 0)
        {
            throw new ToolExecutionException($"QPDF split '{Path.GetFileName(inputPath)}' into nothing.");
        }

        return MoveEach(produced, spec, inputPath, batchRoot);
    }

    private async Task<IReadOnlyList<string>> ExtractImagesAsync(
        JobSpec spec,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var mutool = RequireTool(ExternalToolId.MuPdf);

        var result = await ProcessRunner.RunAsync(
            new ProcessRequest
            {
                FileName = mutool,
                Arguments = PdfCommandBuilder.ExtractImages(Path.GetFullPath(inputPath)),
                WorkingDirectory = stepFolder,
            },
            cancellationToken).ConfigureAwait(false);

        if (!result.IsSuccess)
        {
            throw new ToolExecutionException(
                $"MuPDF could not extract images from '{Path.GetFileName(inputPath)}': {result.DescribeFailure()}",
                result.FullOutput());
        }

        var produced = NaturalSort(Directory.GetFiles(stepFolder));

        if (produced.Count == 0)
        {
            throw new ToolExecutionException($"'{Path.GetFileName(inputPath)}' has no embedded images to extract.");
        }

        return RelocateEach(produced, spec, inputPath, index, batchRoot, path => Path.GetExtension(path).TrimStart('.').ToLowerInvariant());
    }

    private async Task<IReadOnlyList<string>> RenderPagesAsync(
        JobSpec spec,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        string imageFormat,
        CancellationToken cancellationToken)
    {
        var mutool = RequireTool(ExternalToolId.MuPdf);
        var pattern = Path.Combine(stepFolder, $"page-%d.{imageFormat}");
        var dpi = spec.GetInt("dpi", 0) > 0 ? spec.GetInt("dpi", 0) : PdfCommandBuilder.DpiFor(spec.Preset);

        await RunToolAsync(
            mutool, PdfCommandBuilder.RenderPages(inputPath, pattern, dpi), "MuPDF", cancellationToken)
            .ConfigureAwait(false);

        var produced = NaturalSort(Directory.GetFiles(stepFolder, $"page-*.{imageFormat}"));

        if (produced.Count == 0)
        {
            throw new ToolExecutionException($"MuPDF rendered no pages from '{Path.GetFileName(inputPath)}'.");
        }

        return RelocateEach(produced, spec, inputPath, index, batchRoot, _ => imageFormat);
    }

    private async Task<string> ConvertToWordAsync(
        JobSpec spec,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var soffice = RequireTool(ExternalToolId.LibreOffice);

        await RunToolAsync(
            soffice,
            new[] { "--headless", "--convert-to", "docx", "--outdir", stepFolder, Path.GetFullPath(inputPath) },
            "LibreOffice",
            cancellationToken).ConfigureAwait(false);

        var produced = Directory.GetFiles(stepFolder, "*.docx").FirstOrDefault()
            ?? throw new ToolExecutionException($"LibreOffice reported success but wrote nothing for '{Path.GetFileName(inputPath)}'.");

        var outputPath = ResolveOutputPath(spec, inputPath, index, batchRoot);
        return Relocate(produced, outputPath);
    }

    /// <summary>
    /// pdf.convert reads a PDF and rasterises it, or reads an image and assembles it into a
    /// PDF — the direction follows the file it is actually given, not a fixed rule.
    /// </summary>
    private async Task<IReadOnlyList<string>> ConvertPdfOrImageAsync(
        JobSpec spec,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var targetFormat = (spec.Output.Format ?? "pdf").TrimStart('.').ToLowerInvariant();
        var sourceIsPdf = string.Equals(Path.GetExtension(inputPath).TrimStart('.'), "pdf", StringComparison.OrdinalIgnoreCase);

        if (sourceIsPdf && targetFormat != "pdf")
        {
            return await RenderPagesAsync(spec, inputPath, index, batchRoot, stepFolder, targetFormat, cancellationToken)
                .ConfigureAwait(false);
        }

        if (!sourceIsPdf)
        {
            // Forced to "pdf" rather than taken from spec.Output.Format: in a batch that
            // mixes PDF and image files, the picker offers one format for the whole job,
            // and an image input assembled into a PDF must never be named ".jpg" because
            // that was the choice made for a *different* file in the same batch.
            var mutool = RequireTool(ExternalToolId.MuPdf);
            var outputPath = OutputPathResolver.Resolve(
                inputPath, spec.Output with { Format = "pdf" }, index, batchRoot);

            await RunToolAsync(
                mutool, PdfCommandBuilder.ImagesToPdf(new[] { inputPath }, outputPath), "MuPDF", cancellationToken,
                outputPathToDeleteOnCancel: outputPath)
                .ConfigureAwait(false);

            RequireOutput(outputPath, Path.GetFileName(inputPath));
            return new[] { outputPath };
        }

        // PDF to PDF: rewrite through qpdf, which is a well-defined, lossless "convert".
        var qpdf = RequireTool(ExternalToolId.QPdf);
        var rewrittenPath = OutputPathResolver.Resolve(
            inputPath, spec.Output with { Format = "pdf" }, index, batchRoot);

        await RunToolAsync(
            qpdf, PdfCommandBuilder.PassThrough(inputPath, rewrittenPath), "QPDF", cancellationToken,
            outputPathToDeleteOnCancel: rewrittenPath)
            .ConfigureAwait(false);

        RequireOutput(rewrittenPath, Path.GetFileName(inputPath));
        return new[] { rewrittenPath };
    }

    // --- Small helpers ---------------------------------------------------------------------

    private static string StageFor(string operation) => operation switch
    {
        "pdf.compress" => "Compressing",
        "pdf.split" => "Splitting",
        "pdf.to-jpg" or "pdf.convert" => "Rendering",
        "pdf.to-word" => "Converting",
        "pdf.extract-images" => "Extracting",
        _ => "Processing",
    };

    private static string ResolveOutputPath(JobSpec spec, string inputPath, int index, string? batchRoot)
    {
        var format = PdfOperations.FixedFormatFor(spec.OperationId) ?? spec.Output.Format ?? "pdf";
        return OutputPathResolver.Resolve(inputPath, spec.Output with { Format = format }, index, batchRoot);
    }

    private static List<string> MoveEach(
        IReadOnlyList<string> sources, JobSpec spec, string inputPath, string? batchRoot) =>
        RelocateEach(sources, spec, inputPath, startIndex: 1, batchRoot, _ => "pdf");

    /// <summary>
    /// Resolves and moves each produced file in turn — a plain loop rather than a LINQ
    /// projection, because each destination's name depends on which earlier files already
    /// landed in the output folder (see <see cref="OutputPathResolver"/>'s rename-on-collision
    /// policy), so the moves have to happen in order, not just be computed in order.
    /// </summary>
    private static List<string> RelocateEach(
        IReadOnlyList<string> sources,
        JobSpec spec,
        string inputPath,
        int startIndex,
        string? batchRoot,
        Func<string, string> formatForSource)
    {
        var moved = new List<string>(sources.Count);

        for (var i = 0; i < sources.Count; i++)
        {
            var destination = OutputPathResolver.Resolve(
                inputPath, spec.Output with { Format = formatForSource(sources[i]) }, startIndex + i, batchRoot);
            moved.Add(Relocate(sources[i], destination));
        }

        return moved;
    }

    private static string Relocate(string source, string destination)
    {
        var directory = Path.GetDirectoryName(destination);

        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        File.Move(source, destination, overwrite: true);
        return destination;
    }

    /// <summary>Sorts by the first run of digits in the file name, so part-2 sorts before part-10.</summary>
    private static List<string> NaturalSort(IEnumerable<string> paths) => paths
        .OrderBy(path => FirstNumber(Path.GetFileName(path)))
        .ThenBy(path => path, StringComparer.Ordinal)
        .ToList();

    private static int FirstNumber(string fileName)
    {
        var digits = new string(fileName.SkipWhile(c => !char.IsDigit(c)).TakeWhile(char.IsDigit).ToArray());
        return int.TryParse(digits, NumberStyles.Integer, CultureInfo.InvariantCulture, out var number)
            ? number
            : int.MaxValue;
    }

    private static void RequireOutput(string outputPath, string label)
    {
        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"The tool reported success but wrote nothing for '{label}'.");
        }
    }

    private static string RequirePassword(JobSpec spec) =>
        spec.GetOption("password") is { Length: > 0 } password
            ? password
            : throw new ArgumentException("Protecting a PDF needs a password.", nameof(spec));

    private static string RequirePageList(JobSpec spec, string key) =>
        spec.GetOption(key) is { Length: > 0 } pages
            ? pages
            : throw new ArgumentException($"'{spec.OperationId}' needs the '{key}' option to say which pages.", nameof(spec));

    private static int ResolveRotationDegrees(JobSpec spec)
    {
        var degrees = spec.GetInt("degrees", 90);
        return ((degrees % 360) + 360) % 360;
    }

    private static double ResolvePaperWidth(JobSpec spec) =>
        spec.GetDouble("widthPoints", 0) > 0
            ? spec.GetDouble("widthPoints", 0)
            : PdfCommandBuilder.PaperSize(spec.GetOption("paperSize", "a4")).WidthPoints;

    private static double ResolvePaperHeight(JobSpec spec) =>
        spec.GetDouble("heightPoints", 0) > 0
            ? spec.GetDouble("heightPoints", 0)
            : PdfCommandBuilder.PaperSize(spec.GetOption("paperSize", "a4")).HeightPoints;

    private static string ResolvePdfSettings(JobSpec spec) =>
        spec.GetOption("pdfSettings") is { Length: > 0 } explicitSetting
            ? explicitSetting
            : PdfCommandBuilder.PdfSettingsFor(spec.Preset);

    private static IReadOnlySet<int> ParsePageNumbers(string pageList, int totalPages)
    {
        var pages = new HashSet<int>();

        foreach (var token in pageList.Split(',', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries))
        {
            var range = token.Split('-', 2);

            var start = ParsePageNumber(range[0], totalPages);
            var end = range.Length == 2 ? ParsePageNumber(range[1], totalPages) : start;

            for (var page = Math.Min(start, end); page <= Math.Max(start, end); page++)
            {
                pages.Add(page);
            }
        }

        return pages;
    }

    /// <summary>
    /// Recognises the same page-number syntax qpdf's own <c>--pages</c> understands
    /// natively — "z" for the last page, and "rN" counting from the end ("r1" is the last
    /// page, "r2" the second-to-last, and so on. "remove" is the one PDF feature that
    /// pre-resolves its page list itself rather than handing qpdf's own --pages syntax
    /// straight through the way organize/extract-pages/rotate/split-ranges all do (see
    /// <see cref="PdfCommandBuilder.SelectPages"/>) — without this, a user typing "r1" into
    /// "remove" got a confusing "not a valid page number" error even though the exact same
    /// token already works in every other PDF page-selection field.
    /// </summary>
    private static int ParsePageNumber(string token, int totalPages)
    {
        var trimmed = token.Trim();

        if (string.Equals(trimmed, "z", StringComparison.OrdinalIgnoreCase))
        {
            return totalPages;
        }

        if (trimmed.Length > 1 && (trimmed[0] == 'r' || trimmed[0] == 'R')
            && int.TryParse(trimmed.AsSpan(1), NumberStyles.Integer, CultureInfo.InvariantCulture, out var fromEnd)
            && fromEnd > 0)
        {
            return totalPages - fromEnd + 1;
        }

        return int.TryParse(trimmed, NumberStyles.Integer, CultureInfo.InvariantCulture, out var page) && page > 0
            ? page
            : throw new ArgumentException($"'{token}' is not a valid page number.", nameof(token));
    }
}
