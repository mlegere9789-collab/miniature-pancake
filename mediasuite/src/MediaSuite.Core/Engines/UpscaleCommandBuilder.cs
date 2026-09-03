using System.Globalization;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds command lines for Real-ESRGAN's <c>realesrgan-ncnn-vulkan</c> build — the
/// portable, self-contained release the tool manifest bundles, which drives the GPU
/// through Vulkan rather than needing a separate CUDA or PyTorch install. Pure, so every
/// scale and model choice can be checked without the tool installed.
/// </summary>
public static class UpscaleCommandBuilder
{
    /// <summary>
    /// The real model file names shipped in Real-ESRGAN's ncnn-vulkan releases. There is
    /// no single "denoise strength" flag on this build the way the Python package has —
    /// denoising is a completely separate model file, paired with its plain counterpart,
    /// so asking for it just picks the other file.
    /// </summary>
    public static string ModelNameFor(string model, bool denoise) => model.ToLowerInvariant() switch
    {
        "anime" => "realesrgan-x4plus-anime",
        "general" => denoise ? "realesr-general-wdn-x4v3" : "realesr-general-x4v3",
        _ => throw new ArgumentException($"'{model}' is not a model this upscaler offers.", nameof(model)),
    };

    /// <summary>
    /// The individual passes needed to reach a total scale factor. 8x has no model of its
    /// own in these releases, so it is run as a 4x pass followed by a 2x pass over the
    /// result, rather than trusting every build to accept <c>-s 8</c> directly.
    /// </summary>
    public static IReadOnlyList<int> PassScalesFor(int totalScale) => totalScale switch
    {
        2 => new[] { 2 },
        4 => new[] { 4 },
        8 => new[] { 4, 2 },
        _ => throw new ArgumentException($"'{totalScale}x' is not a scale this upscaler offers.", nameof(totalScale)),
    };

    /// <summary>
    /// Builds one upscaling pass.
    /// </summary>
    /// <remarks>
    /// <c>-m</c> names the model folder explicitly rather than relying on the tool's own
    /// "models" default, which is resolved relative to the process's working directory —
    /// and the working directory here is the job's private scratch folder, not wherever
    /// the executable itself lives. <c>-f</c> states the output format explicitly for the
    /// same reason 7-Zip's archive type is stated explicitly rather than left to be
    /// guessed from the file extension.
    /// </remarks>
    public static IReadOnlyList<string> Build(
        string inputPath,
        string outputPath,
        int passScale,
        string modelName,
        string modelsFolder,
        string outputFormat,
        bool forceCpu)
    {
        var arguments = new List<string>
        {
            "-i", inputPath,
            "-o", outputPath,
            "-s", passScale.ToString(CultureInfo.InvariantCulture),
            "-n", modelName,
            "-m", modelsFolder,
            "-f", outputFormat,
        };

        if (forceCpu)
        {
            // -1 is this tool's own way of saying "no GPU device" — the documented
            // fallback for a machine with no usable Vulkan driver.
            arguments.Add("-g");
            arguments.Add("-1");
        }

        return arguments;
    }

    /// <summary>
    /// Sharpens an already-upscaled image through ImageMagick's unsharp mask.
    /// </summary>
    /// <remarks>
    /// Real-ESRGAN has no sharpening of its own — general and anime models alike hand
    /// back exactly what the network produced — so "sharpen" is really a second, ordinary
    /// ImageMagick pass over the result, the same tool the image module already drives.
    /// </remarks>
    public static IReadOnlyList<string> Sharpen(string inputPath, string outputPath) =>
        new[] { inputPath, "-unsharp", "0x1", outputPath };

    /// <summary>
    /// Runs the face-enhance pass: detects faces in an already-upscaled image and
    /// restores each one through GFPGAN, in place over the rest of the image.
    /// </summary>
    /// <remarks>
    /// This tool's own <c>-i</c>/<c>-o</c>/<c>-m</c> flags deliberately mirror
    /// Real-ESRGAN's, the way the rest of this class's command lines do — see
    /// <c>installer/native/face-enhance/face_enhance.cpp</c> for the tool itself.
    /// </remarks>
    public static IReadOnlyList<string> FaceEnhance(string inputPath, string outputPath, string modelsFolder) =>
        new[] { "-i", inputPath, "-o", outputPath, "-m", modelsFolder };
}
