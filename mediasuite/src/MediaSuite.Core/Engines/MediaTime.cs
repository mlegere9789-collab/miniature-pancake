using System.Globalization;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Parses the timecodes a user types into a trim box.
/// </summary>
/// <remarks>
/// Accepts what people actually write — <c>90</c>, <c>1:30</c>, <c>00:01:30.5</c> — rather
/// than forcing one shape, and always writes back the <c>HH:MM:SS.fff</c> form FFmpeg
/// documents, so a locale that uses commas for decimals cannot change the meaning.
/// </remarks>
public static class MediaTime
{
    public static bool TryParse(string? text, out TimeSpan value)
    {
        value = TimeSpan.Zero;

        if (string.IsNullOrWhiteSpace(text))
        {
            return false;
        }

        var trimmed = text.Trim();

        // Plain seconds, possibly fractional.
        if (double.TryParse(trimmed, NumberStyles.Float, CultureInfo.InvariantCulture, out var seconds))
        {
            if (seconds < 0 || double.IsNaN(seconds) || double.IsInfinity(seconds))
            {
                return false;
            }

            value = TimeSpan.FromSeconds(seconds);
            return true;
        }

        var parts = trimmed.Split(':');
        if (parts.Length is < 2 or > 3)
        {
            return false;
        }

        double hours = 0;
        var index = 0;

        if (parts.Length == 3)
        {
            if (!double.TryParse(parts[index++], NumberStyles.Float, CultureInfo.InvariantCulture, out hours))
            {
                return false;
            }
        }

        if (!double.TryParse(parts[index++], NumberStyles.Float, CultureInfo.InvariantCulture, out var minutes)
            || !double.TryParse(parts[index], NumberStyles.Float, CultureInfo.InvariantCulture, out var secondsPart))
        {
            return false;
        }

        if (hours < 0 || minutes < 0 || secondsPart < 0)
        {
            return false;
        }

        value = TimeSpan.FromHours(hours) + TimeSpan.FromMinutes(minutes) + TimeSpan.FromSeconds(secondsPart);
        return true;
    }

    /// <summary>Formats a duration the way FFmpeg expects it on the command line.</summary>
    public static string Format(TimeSpan value)
    {
        // TimeSpan's own "hh" custom format specifier is just the Hours component
        // (0-23), not the total hours -- for any value of a day or more (a plausible
        // trim start/end on a long recording, e.g. "30:00:00" typed directly, which
        // TryParse above accepts without complaint) that silently wraps the hour count
        // mod 24 and drops the day entirely, handing FFmpeg's -ss/-t a seek point up to
        // 24 hours earlier than the one requested. FFmpeg's own duration syntax has no
        // such 24-hour ceiling, so neither does this: the hour count is computed from
        // TotalHours directly and only "mm"/"ss"/"fff" (each already scoped to their own
        // sub-component, never a running total) are taken from the TimeSpan itself.
        var totalHours = (long)value.TotalHours;
        return $"{totalHours:00}:{value.ToString(@"mm\:ss\.fff", CultureInfo.InvariantCulture)}";
    }
}
