namespace MediaSuite.Core.Features;

/// <summary>The AI upscale operation.</summary>
public static class UpscaleOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "upscale.photo",
    };

    /// <summary>An upscaled JPG stays a JPG — there is no "convert while upscaling" choice.</summary>
    public static bool KeepsSourceFormat(string operationId) =>
        string.Equals(operationId, "upscale.photo", StringComparison.OrdinalIgnoreCase);
}
