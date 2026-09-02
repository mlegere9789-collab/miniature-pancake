using System.Globalization;
using System.Text;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds the file list FFmpeg's concat demuxer reads when a GIF is assembled from
/// separate images.
/// </summary>
/// <remarks>
/// The concat demuxer is used rather than a numbered input pattern because the user's
/// files are wherever they dropped them from, with whatever names they already have.
/// </remarks>
public static class ConcatListWriter
{
    /// <summary>
    /// Renders the list. Each entry carries the duration its frame is held for.
    /// </summary>
    /// <remarks>
    /// The final file is repeated on purpose: the concat demuxer applies a
    /// <c>duration</c> to the frame that follows it, so without the repeat the last
    /// image would flash by for a single frame.
    /// </remarks>
    public static string Build(IReadOnlyList<string> imagePaths, TimeSpan frameDuration)
    {
        ArgumentNullException.ThrowIfNull(imagePaths);

        if (imagePaths.Count == 0)
        {
            throw new ArgumentException("A GIF needs at least one image.", nameof(imagePaths));
        }

        var seconds = Math.Max(0.01, frameDuration.TotalSeconds)
            .ToString("0.####", CultureInfo.InvariantCulture);

        var builder = new StringBuilder();

        foreach (var path in imagePaths)
        {
            builder.Append("file '").Append(Escape(path)).AppendLine("'");
            builder.Append("duration ").AppendLine(seconds);
        }

        builder.Append("file '").Append(Escape(imagePaths[^1])).AppendLine("'");

        return builder.ToString();
    }

    /// <summary>
    /// Escapes a path for the single-quoted form the concat demuxer expects. An
    /// apostrophe in a folder name would otherwise end the quoted string early and
    /// break the whole list.
    /// </summary>
    internal static string Escape(string path) => path.Replace("'", @"'\''", StringComparison.Ordinal);
}
