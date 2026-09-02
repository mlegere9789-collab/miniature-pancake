using System.Globalization;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Raster to vector tracing: ImageMagick reduces the image to a bitmap, Potrace traces
/// the outlines into SVG.
/// </summary>
public sealed class PngToSvgEngine : ExternalProcessEngine
{
    public const string OperationId = "image.png-to-svg";

    public PngToSvgEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator)
    {
    }

    public override string Id => "potrace";

    public override string DisplayName => "Potrace";

    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } =
        new[] { ExternalToolId.ImageMagick, ExternalToolId.Potrace };

    public override bool CanHandle(JobSpec spec) =>
        string.Equals(spec.OperationId, OperationId, StringComparison.OrdinalIgnoreCase);

    public override async Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ArgumentNullException.ThrowIfNull(progress);

        string magick;
        string potrace;
        try
        {
            magick = RequireTool(ExternalToolId.ImageMagick);
            potrace = RequireTool(ExternalToolId.Potrace);
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
        var total = Math.Max(1, spec.InputPaths.Count);

        for (var index = 0; index < spec.InputPaths.Count; index++)
        {
            cancellationToken.ThrowIfCancellationRequested();

            var inputPath = spec.InputPaths[index];
            progress.Report(new JobProgress(index * 100d / total, "Tracing", Path.GetFileName(inputPath)));

            if (!File.Exists(inputPath))
            {
                return JobResult.Failure($"'{Path.GetFileName(inputPath)}' no longer exists.");
            }

            try
            {
                var bitmap = Path.Combine(workingDirectory, $"trace-{index}.pbm");
                await RunToolAsync(magick, BuildBitmapArguments(spec, inputPath, bitmap), "ImageMagick", cancellationToken)
                    .ConfigureAwait(false);

                var target = spec.Output with { Format = "svg" };
                var outputPath = OutputPathResolver.Resolve(inputPath, target, index + 1, batchRoot);

                await RunToolAsync(potrace, BuildTraceArguments(spec, bitmap, outputPath), "Potrace", cancellationToken)
                    .ConfigureAwait(false);

                outputs.Add(outputPath);
            }
            catch (ToolExecutionException ex)
            {
                return JobResult.Failure(ex.Message);
            }
        }

        progress.Report(JobProgress.At(100, "Done"));
        return JobResult.Success(outputs, TimeSpan.Zero);
    }

    /// <summary>
    /// Potrace only reads bitmaps, so the image is flattened onto white, turned grey and
    /// thresholded first. The threshold is the single control that most changes the result.
    /// </summary>
    internal static IReadOnlyList<string> BuildBitmapArguments(JobSpec spec, string inputPath, string bitmapPath) =>
        new List<string>
        {
            inputPath,
            "-auto-orient",
            "-background", spec.GetOption("background", "white"),
            "-alpha", "remove",
            "-colorspace", "Gray",
            "-threshold", $"{Math.Clamp(spec.GetInt("threshold", 50), 1, 99).ToString(CultureInfo.InvariantCulture)}%",
            bitmapPath,
        };

    internal static IReadOnlyList<string> BuildTraceArguments(JobSpec spec, string bitmapPath, string outputPath)
    {
        var arguments = new List<string>
        {
            "--svg",
            "--output", outputPath,

            // Drops specks smaller than this many pixels, which otherwise become hundreds
            // of tiny unusable paths.
            "--turdsize", Math.Max(0, spec.GetInt("despeckle", 2)).ToString(CultureInfo.InvariantCulture),

            // How eagerly corners are smoothed into curves.
            "--alphamax", spec.GetDouble("cornerThreshold", 1.0).ToString("0.##", CultureInfo.InvariantCulture),
        };

        if (spec.GetBool("invert", false))
        {
            arguments.Add("--invert");
        }

        arguments.Add(bitmapPath);
        return arguments;
    }
}
