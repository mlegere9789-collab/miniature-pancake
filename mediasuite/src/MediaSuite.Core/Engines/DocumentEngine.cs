using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The document and ebook module: converting among DOCX, DOC, ODT, RTF, TXT, HTML and
/// Markdown, converting among EPUB, MOBI and AZW3, and the two fixed-format bridges to and
/// from PDF (DOCX to PDF, PDF to EPUB) and from EPUB to PDF.
/// </summary>
/// <remarks>
/// Every operation here is one file in, one file out — there is no merge or split the way
/// the PDF module has, so this engine is a straight loop with no batching logic to speak of.
/// </remarks>
public sealed class DocumentEngine : ExternalProcessEngine
{
    public DocumentEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator)
    {
    }

    public override string Id => "document";

    public override string DisplayName => "Documents and Ebooks (Pandoc, LibreOffice, Calibre)";

    /// <remarks>
    /// As with the PDF module, no one tool is required for every operation here: a plain
    /// Markdown-to-HTML conversion only needs Pandoc, and demanding LibreOffice and
    /// Calibre too would block it on a machine that only has Pandoc installed.
    /// </remarks>
    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = Array.Empty<ExternalToolId>();

    public override bool CanHandle(JobSpec spec) => DocumentOperations.All.Contains(spec.OperationId);

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
        var batchRoot = spec.Output.PreserveFolderStructure
            ? OutputPathResolver.FindCommonRoot(spec.InputPaths)
            : null;

        var outputs = new List<string>(spec.InputPaths.Count);
        var total = spec.InputPaths.Count;

        for (var index = 0; index < total; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];
            progress.Report(new JobProgress(index * 100d / total, "Converting", Path.GetFileName(inputPath)));

            var stepFolder = Path.Combine(workingDirectory, index.ToString(System.Globalization.CultureInfo.InvariantCulture));
            Directory.CreateDirectory(stepFolder);

            try
            {
                var output = await ConvertOneAsync(spec, operation, inputPath, index + 1, batchRoot, stepFolder, cancellationToken)
                    .ConfigureAwait(false);
                outputs.Add(output);
            }
            catch (ToolExecutionException ex)
            {
                return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
            {
                return JobResult.Failure($"'{Path.GetFileName(inputPath)}': {ex.Message}");
            }
        }

        progress.Report(JobProgress.At(100, "Done"));
        return JobResult.Success(outputs, TimeSpan.Zero);
    }

    private async Task<string> ConvertOneAsync(
        JobSpec spec,
        string operation,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var targetFormat = ResolveOutputFormat(spec, operation);
        var outputPath = OutputPathResolver.Resolve(inputPath, spec.Output with { Format = targetFormat }, index, batchRoot);

        switch (operation)
        {
            case "document.docx-to-pdf":
                await RunLibreOfficeAsync(inputPath, outputPath, targetFormat, stepFolder, cancellationToken).ConfigureAwait(false);
                break;

            case "pdf.to-epub":
            case "ebook.epub-to-pdf":
            case "ebook.convert":
                await RunCalibreAsync(inputPath, outputPath, cancellationToken).ConfigureAwait(false);
                break;

            case "document.convert":
                await RunDocumentConvertAsync(inputPath, outputPath, targetFormat, stepFolder, cancellationToken)
                    .ConfigureAwait(false);
                break;

            default:
                throw new ArgumentException($"'{operation}' is not a document or ebook operation.", nameof(operation));
        }

        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"The tool reported success but wrote nothing for '{Path.GetFileName(inputPath)}'.");
        }

        return outputPath;
    }

    private async Task RunDocumentConvertAsync(
        string inputPath,
        string outputPath,
        string targetFormat,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var fromExtension = Path.GetExtension(inputPath).TrimStart('.');
        var tool = DocumentCommandBuilder.ToolForDocumentPair(fromExtension, targetFormat);

        if (tool == ExternalToolId.LibreOffice)
        {
            await RunLibreOfficeAsync(inputPath, outputPath, targetFormat, stepFolder, cancellationToken).ConfigureAwait(false);
            return;
        }

        var pandoc = RequireTool(ExternalToolId.Pandoc);
        var arguments = DocumentCommandBuilder.Convert(inputPath, outputPath, fromExtension, targetFormat);
        await RunToolAsync(pandoc, arguments, "Pandoc", cancellationToken, outputPathToDeleteOnCancel: outputPath)
            .ConfigureAwait(false);
    }

    private async Task RunLibreOfficeAsync(
        string inputPath,
        string outputPath,
        string targetFormat,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var soffice = RequireTool(ExternalToolId.LibreOffice);
        var arguments = DocumentCommandBuilder.ConvertViaLibreOffice(Path.GetFullPath(inputPath), stepFolder, targetFormat);

        await RunToolAsync(soffice, arguments, "LibreOffice", cancellationToken).ConfigureAwait(false);

        var produced = Directory.GetFiles(stepFolder, $"*.{targetFormat}").FirstOrDefault();

        if (produced is null)
        {
            return;
        }

        var directory = Path.GetDirectoryName(outputPath);

        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        File.Move(produced, outputPath, overwrite: true);
    }

    private async Task RunCalibreAsync(string inputPath, string outputPath, CancellationToken cancellationToken)
    {
        var ebookConvert = RequireTool(ExternalToolId.Calibre);
        var arguments = DocumentCommandBuilder.ConvertEbook(inputPath, outputPath);
        await RunToolAsync(ebookConvert, arguments, "Calibre", cancellationToken, outputPathToDeleteOnCancel: outputPath)
            .ConfigureAwait(false);
    }

    /// <summary>
    /// The target extension: a format the operation forces, or whatever the user chose.
    /// </summary>
    private static string ResolveOutputFormat(JobSpec spec, string operation)
    {
        if (DocumentOperations.FixedFormatFor(operation) is { Length: > 0 } fixedFormat)
        {
            return fixedFormat;
        }

        if (spec.Output.Format is { Length: > 0 } chosen)
        {
            return chosen.TrimStart('.').ToLowerInvariant();
        }

        throw new ArgumentException($"'{operation}' needs an output format to be chosen.", nameof(spec));
    }
}
