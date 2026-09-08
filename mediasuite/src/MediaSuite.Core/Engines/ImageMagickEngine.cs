using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Image conversion, compression and editing through ImageMagick, with camera RAW
/// decoded by LibRaw first.
/// </summary>
public sealed class ImageMagickEngine : ExternalProcessEngine
{
    private readonly RawImageDecoder _rawDecoder;

    public ImageMagickEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator) =>
        _rawDecoder = new RawImageDecoder(processRunner);

    public override string Id => "imagemagick";

    public override string DisplayName => "ImageMagick";

    /// <remarks>
    /// LibRaw is deliberately not listed: it is only needed for RAW inputs, and requiring
    /// it up front would block ordinary JPEG work on a machine that has no RAW files.
    /// A RAW job without it fails with its own message.
    /// </remarks>
    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } =
        new[] { ExternalToolId.ImageMagick };

    public override bool CanHandle(JobSpec spec) => ImageOperations.All.Contains(spec.OperationId);

    public override async Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(progress);

        string magick;
        try
        {
            magick = RequireTool(ExternalToolId.ImageMagick);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
        }

        var workingDirectory = ResolveWorkingDirectory(spec);
        // spec.BatchRoot is the root JobLauncher computed from the *whole* original
        // batch before splitting it into this one-file spec -- FindCommonRoot(spec.InputPaths)
        // here would just return this single file's own containing folder. The fallback
        // still covers operations that combine their inputs into one spec, where
        // InputPaths already is the whole batch and BatchRoot is left null.
        var batchRoot = spec.Output.PreserveFolderStructure
            ? spec.BatchRoot ?? OutputPathResolver.FindCommonRoot(spec.InputPaths)
            : null;

        var outputs = new List<string>(spec.InputPaths.Count);
        var total = Math.Max(1, spec.InputPaths.Count);

        for (var index = 0; index < spec.InputPaths.Count; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];
            progress.Report(new JobProgress(
                index * 100d / total,
                "Converting",
                Path.GetFileName(inputPath)));

            if (!File.Exists(inputPath))
            {
                return JobResult.Failure($"'{Path.GetFileName(inputPath)}' no longer exists.");
            }

            try
            {
                var output = await ConvertOneAsync(
                    spec, inputPath, index + 1, batchRoot, magick, workingDirectory, progress, cancellationToken)
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
        string inputPath,
        int index,
        string? batchRoot,
        string magick,
        string workingDirectory,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        var sourcePath = inputPath;

        if (ImageOperations.IsRawSource(inputPath))
        {
            progress.Report(new JobProgress(null, "Developing RAW", Path.GetFileName(inputPath)));

            sourcePath = await _rawDecoder.DecodeAsync(
                inputPath,
                workingDirectory,
                RequireTool(ExternalToolId.LibRaw),
                ReadRawOptions(spec),
                cancellationToken).ConfigureAwait(false);
        }

        var target = spec.Output with { Format = ResolveOutputFormat(spec, inputPath) };
        // spec.BatchIndex, not the caller's loop position: JobLauncher splits a
        // multi-file batch into one spec per file, so that loop only ever sees a
        // single item and its own index would always resolve to 1 for every file.
        var outputPath = OutputPathResolver.Resolve(inputPath, target, spec.BatchIndex ?? index, batchRoot);
        var arguments = ImageCommandBuilder.Build(spec, sourcePath, outputPath);

        await RunToolAsync(magick, arguments, "ImageMagick", cancellationToken, outputPathToDeleteOnCancel: outputPath)
            .ConfigureAwait(false);

        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException(
                $"ImageMagick reported success but wrote nothing for '{Path.GetFileName(inputPath)}'.");
        }

        return outputPath;
    }

    /// <summary>
    /// Decides the output extension: an explicit choice wins, then a format the operation
    /// forces ("HEIC to JPG"), then the input's own format for edit-in-place tools.
    /// </summary>
    internal static string ResolveOutputFormat(JobSpec spec, string inputPath)
    {
        if (spec.Output.Format is { Length: > 0 } chosen)
        {
            return chosen.TrimStart('.').ToLowerInvariant();
        }

        if (ImageOperations.FixedFormatFor(spec.OperationId) is { Length: > 0 } fixedFormat)
        {
            return fixedFormat;
        }

        if (ImageOperations.KeepsSourceFormat(spec.OperationId))
        {
            var extension = Path.GetExtension(inputPath).TrimStart('.').ToLowerInvariant();

            // RAW cannot be written back, so an edit on a RAW file lands as TIFF.
            return ImageOperations.IsRawSource(inputPath) || extension.Length == 0 ? "tiff" : extension;
        }

        throw new ArgumentException($"'{spec.OperationId}' needs an output format to be chosen.");
    }

    private static RawDecodeOptions ReadRawOptions(JobSpec spec) => new(
        spec.GetBool("rawCameraWhiteBalance", true),
        spec.GetBool("raw16Bit", true),
        Math.Clamp(spec.GetInt("rawDemosaic", 3), 0, 3));
}
