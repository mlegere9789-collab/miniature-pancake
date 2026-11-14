using System.Globalization;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Video and audio work through FFmpeg: conversion, compression, audio extraction,
/// cropping and trimming.
/// </summary>
public sealed class FFmpegEngine : ExternalProcessEngine
{
    private readonly FFprobeReader _probeReader;

    public FFmpegEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator) =>
        _probeReader = new FFprobeReader(processRunner);

    public override string Id => "ffmpeg";

    public override string DisplayName => "FFmpeg";

    /// <remarks>
    /// FFprobe is not listed. It only supplies the duration behind the percentage and the
    /// codec names behind the remux decision; without it jobs still run, with an
    /// indeterminate bar and a full re-encode.
    /// </remarks>
    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = new[] { ExternalToolId.FFmpeg };

    public override bool CanHandle(JobSpec spec) => FFmpegOperations.All.Contains(spec.OperationId);

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
            return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
        }

        var ffprobe = ToolLocator.Locate(ExternalToolId.FFprobe).Path;
        var batchRoot = spec.Output.PreserveFolderStructure
            ? OutputPathResolver.FindCommonRoot(spec.InputPaths)
            : null;

        var outputs = new List<string>(spec.InputPaths.Count);
        var total = Math.Max(1, spec.InputPaths.Count);

        for (var index = 0; index < spec.InputPaths.Count; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];

            if (!File.Exists(inputPath))
            {
                return JobResult.Failure($"'{Path.GetFileName(inputPath)}' no longer exists.");
            }

            try
            {
                var output = await ConvertOneAsync(
                    spec, inputPath, index, total, batchRoot, ffmpeg, ffprobe, progress, cancellationToken)
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
        int total,
        string? batchRoot,
        string ffmpeg,
        string? ffprobe,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        var fileName = Path.GetFileName(inputPath);
        progress.Report(new JobProgress(index * 100d / total, "Reading", fileName));

        var probe = ffprobe is null
            ? MediaProbe.Unknown
            : await _probeReader.ReadAsync(ffprobe, inputPath, cancellationToken).ConfigureAwait(false);

        var target = spec.Output with { Format = ResolveOutputFormat(spec, inputPath) };
        var outputPath = OutputPathResolver.Resolve(inputPath, target, index + 1, batchRoot);

        var effectiveSpec = WithDurationForBitrateTargeting(spec, probe);
        var arguments = FFmpegCommandBuilder.Build(effectiveSpec, inputPath, outputPath, probe);

        var expected = ExpectedOutputDuration(spec, probe);
        var stage = StageFor(spec.OperationId);

        ProcessResult result;

        try
        {
            result = await ProcessRunner.RunAsync(
                new ProcessRequest
                {
                    FileName = ffmpeg,
                    Arguments = arguments,
                    OnStandardOutputLine = line => ReportProgress(line, progress, expected, index, total, stage, fileName),
                },
                cancellationToken).ConfigureAwait(false);
        }
        catch (OperationCanceledException)
        {
            DeletePartialOutput(outputPath);
            throw;
        }

        if (!result.IsSuccess)
        {
            throw new ToolExecutionException(
                $"FFmpeg could not convert '{fileName}': {result.DescribeFailure()}", result.FullOutput());
        }

        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"FFmpeg reported success but wrote nothing for '{fileName}'.");
        }

        return outputPath;
    }

    private static void ReportProgress(
        string line,
        IProgress<JobProgress> progress,
        TimeSpan? expectedDuration,
        int index,
        int total,
        string stage,
        string fileName)
    {
        var position = FFmpegProgressParser.TryParseTime(line);

        if (position is null)
        {
            return;
        }

        // Without a duration there is nothing to divide by, so the bar stays
        // indeterminate and the stage text carries the information instead.
        if (expectedDuration is not { TotalSeconds: > 0 })
        {
            progress.Report(new JobProgress(null, stage, fileName));
            return;
        }

        var fileFraction = Math.Clamp(position.Value.TotalSeconds / expectedDuration.Value.TotalSeconds, 0, 1);
        var overall = (index + fileFraction) * 100d / total;

        progress.Report(new JobProgress(overall, stage, fileName));
    }

    /// <summary>
    /// A trim's output is only as long as the piece being kept, so progress has to be
    /// measured against that rather than the length of the source.
    /// </summary>
    internal static TimeSpan? ExpectedOutputDuration(JobSpec spec, MediaProbe probe)
    {
        if (string.Equals(spec.OperationId, "video.trim", StringComparison.OrdinalIgnoreCase)
            && FFmpegCommandBuilder.TryReadDuration(spec, out var trimmed)
            && trimmed > TimeSpan.Zero)
        {
            return trimmed;
        }

        return probe.Duration;
    }

    /// <summary>
    /// Size targeting needs the duration to work back to a bitrate, and only the engine
    /// has probed for it.
    /// </summary>
    internal static JobSpec WithDurationForBitrateTargeting(JobSpec spec, MediaProbe probe)
    {
        if (spec.GetDouble("targetSizeMb", 0) <= 0 || probe.Duration is not { TotalSeconds: > 0 } duration)
        {
            return spec;
        }

        return spec.WithOption(
            "durationSeconds",
            duration.TotalSeconds.ToString("0.###", CultureInfo.InvariantCulture));
    }

    internal static string ResolveOutputFormat(JobSpec spec, string inputPath)
    {
        if (spec.Output.Format is { Length: > 0 } chosen)
        {
            return chosen.TrimStart('.').ToLowerInvariant();
        }

        if (FFmpegOperations.FixedFormatFor(spec.OperationId) is { Length: > 0 } fixedFormat)
        {
            return fixedFormat;
        }

        if (FFmpegOperations.KeepsSourceFormat(spec.OperationId))
        {
            var extension = Path.GetExtension(inputPath).TrimStart('.').ToLowerInvariant();
            return extension.Length == 0 ? "mp4" : extension;
        }

        throw new ArgumentException($"'{spec.OperationId}' needs an output format to be chosen.");
    }

    private static string StageFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "video.trim" => "Trimming",
        "video.crop" => "Cropping",
        "video.compress" or "audio.compress.mp3" or "audio.compress.wav" => "Compressing",
        _ => "Encoding",
    };
}
