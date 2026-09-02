using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The Archive Converter: ZIP, 7Z, TAR and GZIP in any direction, plus RAR as a read-only
/// source, all through 7-Zip.
/// </summary>
/// <remarks>
/// 7-Zip cannot convert an archive in one step, so every job is really two: extract the
/// source into a scratch folder, then create a fresh archive of what came out. A GZIP
/// target adds a third step, because gzip compresses a single stream rather than holding
/// several named entries the way zip or 7z do — packing more than one extracted file
/// straight into gzip would silently keep only one of them, so a multi-file GZIP target
/// always goes through an intermediate TAR first, the same way a real <c>.tar.gz</c> is
/// built by hand.
/// </remarks>
public sealed class ArchiveEngine : ExternalProcessEngine
{
    public ArchiveEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator)
    {
    }

    public override string Id => "archive";

    public override string DisplayName => "Archive (7-Zip)";

    /// <remarks>Unlike the PDF and document modules, there is only one tool here, so it can be required up front.</remarks>
    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = new[] { ExternalToolId.SevenZip };

    public override bool CanHandle(JobSpec spec) => ArchiveOperations.All.Contains(spec.OperationId);

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

        string sevenZip;
        try
        {
            sevenZip = RequireTool(ExternalToolId.SevenZip);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message);
        }

        var workingDirectory = ResolveWorkingDirectory(spec);
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
                var output = await ConvertOneAsync(spec, sevenZip, inputPath, index + 1, batchRoot, stepFolder, cancellationToken)
                    .ConfigureAwait(false);
                outputs.Add(output);
            }
            catch (ToolExecutionException ex)
            {
                return JobResult.Failure(ex.Message);
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
        string sevenZip,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var targetFormat = ResolveOutputFormat(spec);
        var archiveType = ArchiveCommandBuilder.ArchiveTypeFor(targetFormat);

        var extractFolder = Path.Combine(stepFolder, "extracted");
        Directory.CreateDirectory(extractFolder);

        await RunToolAsync(sevenZip, ArchiveCommandBuilder.Extract(inputPath, extractFolder), "7-Zip", cancellationToken)
            .ConfigureAwait(false);

        var entries = Directory.GetFileSystemEntries(extractFolder);

        if (entries.Length == 0)
        {
            throw new ToolExecutionException($"'{Path.GetFileName(inputPath)}' extracted to nothing.");
        }

        var outputPath = OutputPathResolver.Resolve(inputPath, spec.Output with { Format = targetFormat }, index, batchRoot);

        if (archiveType == "gzip")
        {
            await CreateGzipAsync(sevenZip, entries, outputPath, stepFolder, cancellationToken).ConfigureAwait(false);
        }
        else
        {
            await RunToolAsync(
                sevenZip, ArchiveCommandBuilder.CreateArchive(outputPath, entries, archiveType), "7-Zip", cancellationToken)
                .ConfigureAwait(false);
        }

        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"7-Zip reported success but wrote nothing for '{Path.GetFileName(inputPath)}'.");
        }

        return outputPath;
    }

    /// <summary>
    /// Gzip holds one stream, not several named entries, so anything extracted goes into a
    /// TAR first and that TAR is what actually gets gzipped — the standard shape of a real
    /// <c>.tar.gz</c>, applied even when there is only a single extracted file, so the
    /// result is uniform regardless of what the source archive happened to contain.
    /// </summary>
    private async Task CreateGzipAsync(
        string sevenZip,
        IReadOnlyList<string> entries,
        string outputPath,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var tarPath = Path.Combine(stepFolder, "bundle.tar");

        await RunToolAsync(sevenZip, ArchiveCommandBuilder.CreateArchive(tarPath, entries, "tar"), "7-Zip", cancellationToken)
            .ConfigureAwait(false);

        if (!File.Exists(tarPath))
        {
            throw new ToolExecutionException("7-Zip reported success but wrote no intermediate TAR for the GZIP target.");
        }

        await RunToolAsync(
            sevenZip, ArchiveCommandBuilder.CreateArchive(outputPath, new[] { tarPath }, "gzip"), "7-Zip", cancellationToken)
            .ConfigureAwait(false);
    }

    private static string ResolveOutputFormat(JobSpec spec) =>
        spec.Output.Format is { Length: > 0 } chosen
            ? chosen.TrimStart('.').ToLowerInvariant()
            : throw new ArgumentException("'archive.convert' needs an output format to be chosen.", nameof(spec));
}
