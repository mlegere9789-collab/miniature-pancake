using System.Globalization;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The GIF tools: video to GIF and back, GIF from a set of images, and GIF compression.
/// </summary>
/// <remarks>
/// Separate from <see cref="FFmpegEngine"/> even though both drive FFmpeg, because a GIF
/// job is a small pipeline rather than one command: a palette pass feeds an encode pass,
/// and the slideshow tools write a concat list first.
/// </remarks>
public sealed class GifEngine : ExternalProcessEngine
{
    private readonly FFprobeReader _probeReader;

    public GifEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator) =>
        _probeReader = new FFprobeReader(processRunner);

    public override string Id => "gif";

    public override string DisplayName => "GIF (FFmpeg)";

    /// <remarks>FFprobe is optional here for the same reason as in the video engine: without
    /// it the bar is indeterminate, but every job still runs.</remarks>
    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = new[] { ExternalToolId.FFmpeg };

    public override bool CanHandle(JobSpec spec) => GifOperations.All.Contains(spec.OperationId);

    public override async Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(progress);

        string ffmpeg;
        try
        {
            ffmpeg = RequireTool(ExternalToolId.FFmpeg);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message);
        }

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

        var ffprobe = ToolLocator.Locate(ExternalToolId.FFprobe).Path;
        var scratch = ResolveWorkingDirectory(spec);

        try
        {
            List<string> outputs;

            if (GifOperations.CombinesInputs(spec.OperationId))
            {
                var single = await BuildSlideshowAsync(spec, ffmpeg, scratch, progress, cancellationToken)
                    .ConfigureAwait(false);
                outputs = new List<string> { single };
            }
            else
            {
                outputs = await ConvertEachAsync(spec, ffmpeg, ffprobe, scratch, progress, cancellationToken)
                    .ConfigureAwait(false);
            }

            progress.Report(JobProgress.At(100, "Done"));
            return JobResult.Success(outputs, TimeSpan.Zero);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or ArgumentException)
        {
            return JobResult.Failure(ex.Message);
        }
    }

    private async Task<List<string>> ConvertEachAsync(
        JobSpec spec,
        string ffmpeg,
        string? ffprobe,
        string scratch,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        var batchRoot = spec.Output.PreserveFolderStructure
            ? OutputPathResolver.FindCommonRoot(spec.InputPaths)
            : null;

        var outputs = new List<string>(spec.InputPaths.Count);
        var total = spec.InputPaths.Count;

        for (var index = 0; index < total; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];
            var fileName = Path.GetFileName(inputPath);
            progress.Report(new JobProgress(index * 100d / total, "Reading", fileName));

            var probe = ffprobe is null
                ? MediaProbe.Unknown
                : await _probeReader.ReadAsync(ffprobe, inputPath, cancellationToken).ConfigureAwait(false);

            var outputPath = ResolveOutputPath(spec, inputPath, index + 1, batchRoot);
            var stepFolder = CreateStepFolder(scratch, index);
            var plan = GifCommandBuilder.Build(spec, new GifSource(inputPath), outputPath, stepFolder);

            await RunPlanAsync(
                plan, ffmpeg, ExpectedDuration(spec, probe.Duration), index, total, fileName,
                progress, cancellationToken).ConfigureAwait(false);

            RequireOutput(outputPath, fileName);
            outputs.Add(outputPath);
        }

        return outputs;
    }

    private async Task<string> BuildSlideshowAsync(
        JobSpec spec,
        string ffmpeg,
        string scratch,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        var frameDuration = GifCommandBuilder.ResolveFrameDuration(spec);
        var listPath = Path.Combine(scratch, "frames.txt");

        // UTF-8 without a byte-order mark: the concat demuxer reads the list as plain text
        // and treats a BOM as part of the first directive.
        await File.WriteAllTextAsync(
            listPath,
            ConcatListWriter.Build(spec.InputPaths, frameDuration),
            new System.Text.UTF8Encoding(encoderShouldEmitUTF8Identifier: false),
            cancellationToken).ConfigureAwait(false);

        var outputPath = ResolveOutputPath(spec, spec.InputPaths[0], 1, batchRoot: null);
        var stepFolder = CreateStepFolder(scratch, 0);
        var plan = GifCommandBuilder.Build(spec, new GifSource(listPath, IsConcatList: true), outputPath, stepFolder);

        var expected = frameDuration * spec.InputPaths.Count;
        var label = $"{spec.InputPaths.Count} images";

        await RunPlanAsync(plan, ffmpeg, expected, 0, 1, label, progress, cancellationToken).ConfigureAwait(false);

        RequireOutput(outputPath, label);
        return outputPath;
    }

    private async Task RunPlanAsync(
        GifPlan plan,
        string ffmpeg,
        TimeSpan? expectedDuration,
        int index,
        int total,
        string label,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        for (var step = 0; step < plan.Steps.Count; step++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var current = plan.Steps[step];
            var stepIndex = step;

            var result = await ProcessRunner.RunAsync(
                new ProcessRequest
                {
                    FileName = ffmpeg,
                    Arguments = current.Arguments,
                    OnStandardOutputLine = line => ReportProgress(
                        line, progress, expectedDuration, index, total, stepIndex, plan.Steps.Count,
                        current.Description, label),
                },
                cancellationToken).ConfigureAwait(false);

            if (!result.IsSuccess)
            {
                throw new ToolExecutionException(
                    $"FFmpeg could not process '{label}' ({current.Description.ToLowerInvariant()}): "
                    + result.DescribeFailure());
            }
        }
    }

    /// <summary>
    /// Folds a step's own progress into the job's. Both passes read the whole input, so
    /// they are given an equal share rather than guessing which is slower.
    /// </summary>
    private static void ReportProgress(
        string line,
        IProgress<JobProgress> progress,
        TimeSpan? expectedDuration,
        int index,
        int total,
        int step,
        int steps,
        string stage,
        string label)
    {
        var position = FFmpegProgressParser.TryParseTime(line);

        if (position is null)
        {
            return;
        }

        if (expectedDuration is not { TotalSeconds: > 0 })
        {
            progress.Report(new JobProgress(null, stage, label));
            return;
        }

        var withinStep = Math.Clamp(position.Value.TotalSeconds / expectedDuration.Value.TotalSeconds, 0, 1);
        var withinFile = (step + withinStep) / steps;
        progress.Report(new JobProgress((index + withinFile) * 100d / total, stage, label));
    }

    private static string ResolveOutputPath(JobSpec spec, string inputPath, int index, string? batchRoot)
    {
        // Every GIF tool has exactly one possible output format, so the picker never offers
        // a choice and the operation decides.
        var format = GifOperations.FixedFormatFor(spec.OperationId)
            ?? throw new ArgumentException($"'{spec.OperationId}' is not a GIF operation.", nameof(spec));

        return OutputPathResolver.Resolve(inputPath, spec.Output with { Format = format }, index, batchRoot);
    }

    private static string CreateStepFolder(string scratch, int index)
    {
        // One folder per input so a batch's palettes cannot tread on each other.
        var folder = Path.Combine(scratch, index.ToString(CultureInfo.InvariantCulture));
        Directory.CreateDirectory(folder);
        return folder;
    }

    private static void RequireOutput(string outputPath, string label)
    {
        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"FFmpeg reported success but wrote nothing for '{label}'.");
        }
    }

    /// <summary>
    /// A trimmed GIF is only as long as the piece being kept, so progress is measured
    /// against that rather than the length of the source clip.
    /// </summary>
    internal static TimeSpan? ExpectedDuration(JobSpec spec, TimeSpan? probed)
    {
        if (FFmpegCommandBuilder.TryReadDuration(spec, out var trimmed) && trimmed > TimeSpan.Zero)
        {
            return trimmed;
        }

        if (probed is not { TotalSeconds: > 0 } duration)
        {
            return null;
        }

        // FFmpeg reports the output timeline, which starts at zero after a seek.
        if (!MediaTime.TryParse(spec.GetOption("start"), out var start) || start <= TimeSpan.Zero)
        {
            return duration;
        }

        // A start past the end of the clip leaves nothing to measure against, so fall back
        // to an indeterminate bar rather than dividing by a negative.
        var remaining = duration - start;
        return remaining > TimeSpan.Zero ? remaining : null;
    }
}
