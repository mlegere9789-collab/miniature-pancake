using MediaSuite.Core.Features;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The AI Photo Upscaler: 2x, 4x or 8x with general or anime models, optional denoise,
/// sharpen and face enhance, through Real-ESRGAN's ncnn-vulkan build.
/// </summary>
/// <remarks>
/// "Face enhance" is an optional extra pass, not a third model alongside general/anime —
/// the same shape the real upstream <c>realesrgan</c> Python CLI's own <c>--face_enhance</c>
/// flag has, layering GFPGAN on top of whichever base model actually upscaled the image.
/// It runs through a separate compiled-from-source tool
/// (<see cref="ExternalToolId.GfpganFaceEnhance"/>; see
/// <c>installer/native/face-enhance/README.md</c> for where its source and models come
/// from) rather than Real-ESRGAN itself, which has no face-restoration model of its own —
/// and it is genuinely optional at the tool level: unlike <see cref="RequiredTools"/>,
/// nothing here fails if it is missing unless a job actually asks for it, the same way
/// <c>sharpen</c> only needs ImageMagick when a job actually sets it.
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

        var faceEnhance = spec.GetBool("faceEnhance", false);
        string? faceEnhanceExe = null;
        string? faceEnhanceModelsFolder = null;

        if (faceEnhance)
        {
            try
            {
                faceEnhanceExe = RequireTool(ExternalToolId.GfpganFaceEnhance);
            }
            catch (ToolExecutionException ex)
            {
                return JobResult.Failure(ex.Message, diagnostics: ex.Diagnostics);
            }

            faceEnhanceModelsFolder = Path.Combine(Path.GetDirectoryName(faceEnhanceExe) ?? string.Empty, "models");
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
                    spec, realEsrgan, magick, faceEnhanceExe, faceEnhanceModelsFolder, modelsFolder,
                    inputPath, index + 1, batchRoot, stepFolder, cancellationToken)
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
        string? faceEnhanceExe,
        string? faceEnhanceModelsFolder,
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
        var faceEnhance = faceEnhanceExe is not null;

        // The last upscale pass writes straight to the final output, unless a sharpen
        // and/or face-enhance pass still has to run afterwards — in which case it writes
        // to a scratch file that the first of those reads from instead. Whichever of
        // sharpen/face-enhance actually runs last is the one that writes to outputPath;
        // an earlier one in the chain writes to its own scratch file for the next to read.
        var hasExtraPass = sharpen || faceEnhance;
        var finalUpscaledPath = hasExtraPass ? Path.Combine(stepFolder, $"upscaled.{targetFormat}") : outputPath;

        var currentInput = inputPath;

        for (var pass = 0; pass < passScales.Count; pass++)
        {
            var isLastPass = pass == passScales.Count - 1;
            var passOutput = isLastPass ? finalUpscaledPath : Path.Combine(stepFolder, $"pass{pass}.{targetFormat}");

            var arguments = UpscaleCommandBuilder.Build(
                currentInput, passOutput, passScales[pass], modelName, modelsFolder, targetFormat, forceCpu);

            // passOutput only equals outputPath on the pass that writes the real, final file
            // directly (the last pass with no sharpen/face-enhance to follow) -- every other
            // pass writes to its own scratch file in stepFolder, so passing outputPath there
            // too would risk deleting a pre-existing file under OverwritePolicy.Overwrite
            // that this particular pass never touched.
            await RunToolAsync(
                realEsrgan, arguments, "Real-ESRGAN", cancellationToken,
                outputPathToDeleteOnCancel: passOutput == outputPath ? outputPath : null)
                .ConfigureAwait(false);
            RequireOutput(passOutput, Path.GetFileName(inputPath));

            currentInput = passOutput;
        }

        var currentPath = finalUpscaledPath;

        if (sharpen)
        {
            var sharpenOutput = faceEnhance ? Path.Combine(stepFolder, $"sharpened.{targetFormat}") : outputPath;

            await RunToolAsync(
                magick!, UpscaleCommandBuilder.Sharpen(currentPath, sharpenOutput), "ImageMagick", cancellationToken,
                outputPathToDeleteOnCancel: sharpenOutput == outputPath ? outputPath : null)
                .ConfigureAwait(false);
            RequireOutput(sharpenOutput, Path.GetFileName(inputPath));

            currentPath = sharpenOutput;
        }

        if (faceEnhance)
        {
            await RunToolAsync(
                faceEnhanceExe!,
                UpscaleCommandBuilder.FaceEnhance(currentPath, outputPath, faceEnhanceModelsFolder!),
                "Face Enhance",
                cancellationToken,
                outputPathToDeleteOnCancel: outputPath)
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
