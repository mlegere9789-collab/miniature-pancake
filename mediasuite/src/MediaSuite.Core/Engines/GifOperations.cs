namespace MediaSuite.Core.Engines;

/// <summary>The GIF operations, and what each implies about inputs and output format.</summary>
public static class GifOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "gif.from-video",
        "gif.mp4-to-gif",
        "gif.webm-to-gif",
        "gif.mov-to-gif",
        "gif.avi-to-gif",
        "gif.apng-to-gif",
        "gif.from-images",
        "gif.maker",
        "gif.compress",
        "gif.to-mp4",
        "gif.to-apng",
    };

    public static string? FixedFormatFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "gif.to-mp4" => "mp4",
        "gif.to-apng" => "apng",
        var id when All.Contains(id) => "gif",
        _ => null,
    };

    /// <summary>
    /// Operations that turn many inputs into one output, so the queue must not split them
    /// into a job per file the way it does for conversions.
    /// </summary>
    public static bool CombinesInputs(string operationId) => operationId.ToLowerInvariant() switch
    {
        "gif.from-images" or "gif.maker" => true,
        _ => false,
    };

    /// <summary>True when the output is a GIF built from frames, so it needs a palette.</summary>
    public static bool ProducesGif(string operationId) =>
        string.Equals(FixedFormatFor(operationId), "gif", StringComparison.OrdinalIgnoreCase);
}
