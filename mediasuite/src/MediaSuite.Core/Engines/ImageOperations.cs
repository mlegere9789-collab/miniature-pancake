using MediaSuite.Core.Formats;

namespace MediaSuite.Core.Engines;

/// <summary>
/// The image operations the ImageMagick engine claims, and the fixed output format that
/// some of them imply.
/// </summary>
public static class ImageOperations
{
    /// <summary>Every operation id handled by <see cref="ImageMagickEngine"/>.</summary>
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "image.convert",
        "image.webp-to-png",
        "image.jfif-to-png",
        "image.heic-to-jpg",
        "image.heic-to-png",
        "image.webp-to-jpg",
        "image.svg-convert",
        "image.compress",
        "image.compress.jpeg",
        "image.compress.png",
        "image.resize",
        "image.crop",
        "image.rotate",
        "image.flip",
        "image.enlarge",
    };

    /// <summary>
    /// Output format implied by the operation itself — "HEIC to JPG" cannot write anything
    /// but JPEG. Null when the user chooses the format.
    /// </summary>
    public static string? FixedFormatFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "image.webp-to-png" => "png",
        "image.jfif-to-png" => "png",
        "image.heic-to-png" => "png",
        "image.heic-to-jpg" => "jpg",
        "image.webp-to-jpg" => "jpg",
        "image.compress.jpeg" => "jpg",
        "image.compress.png" => "png",
        _ => null,
    };

    /// <summary>
    /// True for operations that edit an image in place — they keep the input's format
    /// unless the user asks for a different one.
    /// </summary>
    public static bool KeepsSourceFormat(string operationId) => operationId.ToLowerInvariant() switch
    {
        "image.resize" or "image.crop" or "image.rotate" or "image.flip" or "image.enlarge" => true,
        "image.compress" => true,
        _ => false,
    };

    /// <summary>Formats that are drawings rather than pixels, and need a render resolution.</summary>
    public static bool IsVectorSource(string path) =>
        FormatCatalog.KindOf(path) == MediaKind.Vector;

    /// <summary>Camera RAW needs decoding by LibRaw before ImageMagick sees it.</summary>
    public static bool IsRawSource(string path) =>
        FormatCatalog.KindOf(path) == MediaKind.RawImage;
}
