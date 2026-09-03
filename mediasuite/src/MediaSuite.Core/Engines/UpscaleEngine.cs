using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The AI Photo Upscaler: 2x, 4x or 8x with general or anime models, optional denoise and
/// sharpen, through Real-ESRGAN's ncnn-vulkan build.
/// </summary>
/// <remarks>
/// "Face enhance", listed in the brief alongside general and anime, is deliberately not
/// implemented here: it is a separate face-restoration model (GFPGAN) that the bundled
/// Real-ESRGAN release does not include and the tool manifest does not list. Adding it
/// properly means bundling a second AI model and binary, not a flag on this one, so it is
/// left for a follow-up rather than silently faked with the general model.
/// </remarks>
public sealed class UpscaleEngine : ExternalProcessEngine
{
    public UpscaleEngine(IProcessRunner processRunner, ToolLocator toolLocator)
        : base(processRunner, toolLocator)
    {
    }

    public override string Id => "upscale";

    public override string DisplayName => "AI Upscale (Real-ESRGAN)";

    public override IReadOnlyList<ExternalToolId> RequiredTools { get; } = new[] { ExternalToolId.RealEsrgan };

    public override bool CanHandle(JobSpec spec) => UpscaleOperations.All.Contains(spec.OperationId);

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

        string realEsrgan;
        try
        {
            realEsrgan = RequireTool(ExternalToolId.RealEsrgan);
        }
        catch (ToolExecutionException ex)
        {
            return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
        }

        var sharpen = spec.GetBool("sharpen", false);
        string? magick = null;

        if (sharpen)
        {
            try
            {
                magick = RequireTool(ExternalToolId.ImageMagick);
            }
            catch (ToolExecutionException ex)
            {
                return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
            }
        }

        // The tool's own "models" default is resolved next to whatever the current
        // working directory happens to be, which here is the job's scratch folder rather
        // than the tool's own install folder — so the model path is always stated
        // explicitly, right next to the executable that ships with it.
        var modelsFolder = Path.Combine(Path.GetDirectoryName(realEsrgan) ?? string.Empty, "models");

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
            progress.Report(new JobProgress(index * 100d / total, "Upscaling", Path.GetFileName(inputPath)));

            var stepFolder = Path.Combine(workingDirectory, index.ToString(System.Globalization.CultureInfo.InvariantCulture));
            Directory.CreateDirectory(stepFolder);

            try
            {
                var output = await UpscaleOneAsync(
                    spec, realEsrgan, magick, modelsFolder, inputPath, index + 1, batchRoot, stepFolder, cancellationToken)
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

    private async Task<string> UpscaleOneAsync(
        JobSpec spec,
        string realEsrgan,
        string? magick,
        string modelsFolder,
        string inputPath,
        int index,
        string? batchRoot,
        string stepFolder,
        CancellationToken cancellationToken)
    {
        var targetFormat = ResolveOutputFormat(spec, inputPath);
        var outputPath = OutputPathResolver.Resolve(inputPath, spec.Output with { Format = targetFormat }, index, batchRoot);

        var passScales = UpscaleCommandBuilder.PassScalesFor(spec.GetInt("scale", 4));
        var modelName = UpscaleCommandBuilder.ModelNameFor(spec.GetOption("model", "general"), spec.GetBool("denoise", false));
        var forceCpu = spec.GetBool("forceCpu", false);
        var sharpen = spec.GetBool("sharpen", false);

        // The last upscale pass writes straight to the final output, unless a sharpen
        // pass still has to run afterwards — in which case it writes to a scratch file
        // that the sharpen pass reads from instead.
        var finalUpscaledPath = sharpen ? Path.Combine(stepFolder, $"upscaled.{targetFormat}") : outputPath;

        var currentInput = inputPath;

        for (var pass = 0; pass < passScales.Count; pass++)
        {
            var isLastPass = pass == passScales.Count - 1;
            var passOutput = isLastPass ? finalUpscaledPath : Path.Combine(stepFolder, $"pass{pass}.{targetFormat}");

            var arguments = UpscaleCommandBuilder.Build(
                currentInput, passOutput, passScales[pass], modelName, modelsFolder, targetFormat, forceCpu);

            await RunToolAsync(realEsrgan, arguments, "Real-ESRGAN", cancellationToken).ConfigureAwait(false);
            RequireOutput(passOutput, Path.GetFileName(inputPath));

            currentInput = passOutput;
        }

        if (sharpen)
        {
            await RunToolAsync(
                magick!, UpscaleCommandBuilder.Sharpen(finalUpscaledPath, outputPath), "ImageMagick", cancellationToken)
                .ConfigureAwait(false);
            RequireOutput(outputPath, Path.GetFileName(inputPath));
        }

        return outputPath;
    }

    private static string ResolveOutputFormat(JobSpec spec, string inputPath)
    {
        if (spec.Output.Format is { Length: > 0 } chosen)
        {
            return chosen.TrimStart('.').ToLowerInvariant();
        }

        var extension = Path.GetExtension(inputPath).TrimStart('.').ToLowerInvariant();
        return extension.Length == 0 ? "png" : extension;
    }

    private static void RequireOutput(string outputPath, string label)
    {
        if (!File.Exists(outputPath))
        {
            throw new ToolExecutionException($"The tool reported success but wrote nothing for '{label}'.");
        }
    }
}
