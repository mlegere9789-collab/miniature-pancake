using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Decodes a camera RAW file to a TIFF that ImageMagick can read, using LibRaw's
/// <c>dcraw_emu</c>.
/// </summary>
public sealed class RawImageDecoder
{
    private readonly IProcessRunner _processRunner;

    public RawImageDecoder(IProcessRunner processRunner) =>
        _processRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));

    /// <summary>
    /// Decodes <paramref name="rawPath"/> and returns the path of the TIFF produced inside
    /// <paramref name="workingDirectory"/>.
    /// </summary>
    /// <remarks>
    /// The RAW is copied into a private folder first because dcraw_emu writes its output
    /// beside its input — decoding in place would drop a TIFF into the user's photo
    /// library. The copy costs a file write but keeps the source folder untouched.
    /// </remarks>
    public async Task<string> DecodeAsync(
        string rawPath,
        string workingDirectory,
        string dcrawExecutable,
        RawDecodeOptions options,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(rawPath);
        ArgumentException.ThrowIfNullOrWhiteSpace(workingDirectory);
        ArgumentNullException.ThrowIfNull(options);

        var decodeFolder = Path.Combine(workingDirectory, "raw-" + Guid.NewGuid().ToString("N")[..8]);
        Directory.CreateDirectory(decodeFolder);

        var copiedRaw = Path.Combine(decodeFolder, Path.GetFileName(rawPath));
        File.Copy(rawPath, copiedRaw, overwrite: true);

        var result = await _processRunner.RunAsync(
            new ProcessRequest
            {
                FileName = dcrawExecutable,
                Arguments = BuildArguments(copiedRaw, options),
                WorkingDirectory = decodeFolder,
            },
            cancellationToken).ConfigureAwait(false);

        if (!result.IsSuccess)
        {
            throw new ToolExecutionException(
                $"Could not decode '{Path.GetFileName(rawPath)}': {result.DescribeFailure()}");
        }

        return FindDecodedFile(decodeFolder, copiedRaw, rawPath);
    }

    /// <summary>
    /// dcraw_emu names its output from the input, and the exact suffix has varied between
    /// LibRaw versions, so take whatever new image file appeared instead of guessing.
    /// </summary>
    private static string FindDecodedFile(string decodeFolder, string copiedRaw, string originalRaw)
    {
        var produced = Directory
            .EnumerateFiles(decodeFolder)
            .Where(file => !string.Equals(file, copiedRaw, StringComparison.OrdinalIgnoreCase))
            .OrderByDescending(file => new FileInfo(file).Length)
            .ToList();

        if (produced.Count == 0)
        {
            throw new ToolExecutionException(
                $"LibRaw reported success but produced no image for '{Path.GetFileName(originalRaw)}'.");
        }

        return produced[0];
    }

    private static IReadOnlyList<string> BuildArguments(string rawPath, RawDecodeOptions options)
    {
        var arguments = new List<string> { "-T" };

        if (options.UseCameraWhiteBalance)
        {
            arguments.Add("-w");
        }

        if (options.SixteenBit)
        {
            arguments.Add("-6");
        }

        arguments.Add("-q");
        arguments.Add(options.DemosaicQuality.ToString(System.Globalization.CultureInfo.InvariantCulture));

        arguments.Add(rawPath);
        return arguments;
    }
}

/// <summary>How a RAW file should be developed before conversion.</summary>
/// <param name="UseCameraWhiteBalance">Use the camera's own white balance rather than a flat one.</param>
/// <param name="SixteenBit">Keep 16 bits per channel, worth it when the output is TIFF or PNG.</param>
/// <param name="DemosaicQuality">LibRaw demosaic algorithm, 0 (fast) to 3 (AHD, best).</param>
public sealed record RawDecodeOptions(
    bool UseCameraWhiteBalance = true,
    bool SixteenBit = true,
    int DemosaicQuality = 3)
{
    public static RawDecodeOptions Default { get; } = new();
}
