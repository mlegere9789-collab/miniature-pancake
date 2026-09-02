namespace MediaSuite.Core.Settings;

/// <summary>
/// The "key=value" per-line text format the Custom preset editor uses, so free-form
/// advanced options can round-trip through a single multi-line textbox instead of a
/// bespoke control per operation.
/// </summary>
public static class OptionsTextFormat
{
    /// <summary>
    /// Parses "key=value" lines into an options dictionary. Blank lines, lines with no
    /// "=", and lines with a blank key are skipped rather than rejected, since this text
    /// comes straight out of a textbox the user may still be editing.
    /// </summary>
    public static IReadOnlyDictionary<string, string> Parse(string? text)
    {
        var options = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);

        if (string.IsNullOrWhiteSpace(text))
        {
            return options;
        }

        foreach (var rawLine in text.Split('\n'))
        {
            var line = rawLine.TrimEnd('\r').Trim();
            if (line.Length == 0)
            {
                continue;
            }

            var separatorIndex = line.IndexOf('=');
            if (separatorIndex <= 0)
            {
                continue;
            }

            var key = line[..separatorIndex].Trim();
            var value = line[(separatorIndex + 1)..].Trim();

            if (key.Length > 0)
            {
                options[key] = value;
            }
        }

        return options;
    }

    /// <summary>Formats an options dictionary back into "key=value" lines, for editing.</summary>
    public static string Format(IReadOnlyDictionary<string, string> options) =>
        string.Join('\n', options.Select(pair => $"{pair.Key}={pair.Value}"));
}
