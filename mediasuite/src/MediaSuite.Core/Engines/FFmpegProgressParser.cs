using System.Globalization;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Reads FFmpeg's <c>-progress</c> stream, which emits <c>key=value</c> lines rather than
/// the human-readable status line.
/// </summary>
public static class FFmpegProgressParser
{
    /// <summary>
    /// Position reached, or null when the line carries no usable timestamp.
    /// </summary>
    /// <remarks>
    /// Only <c>out_time</c> and <c>out_time_us</c> are read. <c>out_time_ms</c> is skipped
    /// on purpose: FFmpeg has long written microseconds into that field despite the name,
    /// so trusting it would put progress out by a factor of a thousand.
    /// </remarks>
    public static TimeSpan? TryParseTime(string? line)
    {
        if (string.IsNullOrWhiteSpace(line))
        {
            return null;
        }

        var separator = line.IndexOf('=');
        if (separator <= 0)
        {
            return null;
        }

        var key = line[..separator].Trim();
        var value = line[(separator + 1)..].Trim();

        if (value.Length == 0 || value.Equals("N/A", StringComparison.OrdinalIgnoreCase))
        {
            return null;
        }

        return key switch
        {
            "out_time" => MediaTime.TryParse(value, out var parsed) ? parsed : null,
            "out_time_us" => long.TryParse(value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var microseconds)
                && microseconds >= 0
                    ? TimeSpan.FromMilliseconds(microseconds / 1000d)
                    : null,
            _ => null,
        };
    }

    /// <summary>True for the line FFmpeg writes once the run has finished.</summary>
    public static bool IsCompletion(string? line) =>
        line is not null && line.Trim().Equals("progress=end", StringComparison.OrdinalIgnoreCase);
}
