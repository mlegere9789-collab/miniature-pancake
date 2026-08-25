using MediaSuite.Core.Formats;

namespace MediaSuite.Core.Features;

/// <summary>
/// Maps an operation id to the kind of file it produces, so the UI can offer the right
/// output formats without hard-coding a list per screen.
/// </summary>
public static class OperationFamily
{
    /// <summary>
    /// Output family for an operation. Operations that force their own format (say
    /// "HEIC to JPG") do not need this — the format is not the user's to choose.
    /// </summary>
    public static MediaKind OutputKindFor(string operationId)
    {
        if (string.IsNullOrWhiteSpace(operationId))
        {
            return MediaKind.Unknown;
        }

        var family = operationId.Split('.', 2)[0].ToLowerInvariant();

        return family switch
        {
            "image" => MediaKind.Image,
            "gif" => MediaKind.Animation,
            "video" => MediaKind.Video,
            "audio" => MediaKind.Audio,
            "pdf" => MediaKind.Pdf,
            "document" => MediaKind.Document,
            "ebook" => MediaKind.Ebook,
            "archive" => MediaKind.Archive,
            _ => MediaKind.Unknown,
        };
    }

    /// <summary>Formats a given operation can reasonably be asked to write.</summary>
    public static IReadOnlyList<FileFormat> OutputFormatsFor(string operationId)
    {
        var kind = OutputKindFor(operationId);

        if (kind == MediaKind.Unknown)
        {
            return Array.Empty<FileFormat>();
        }

        var formats = FormatCatalog.OfKind(kind).Where(format => format.CanWrite).ToList();

        // Still images and vectors are interchangeable as outputs: rasterising an SVG to
        // PNG and tracing a PNG to SVG are both things the image tools do.
        if (kind == MediaKind.Image)
        {
            formats.AddRange(FormatCatalog.OfKind(MediaKind.Vector).Where(format => format.CanWrite));
        }

        return formats;
    }
}
