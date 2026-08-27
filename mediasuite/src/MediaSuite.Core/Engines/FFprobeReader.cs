using System.Globalization;
using System.Text.Json;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Asks FFprobe for a file's duration and codecs, so progress can be a percentage and a
/// conversion can decide whether it is allowed to remux instead of re-encode.
/// </summary>
public sealed class FFprobeReader
{
    private readonly IProcessRunner _processRunner;

    public FFprobeReader(IProcessRunner processRunner) =>
        _processRunner = processRunner ?? throw new ArgumentNullException(nameof(processRunner));

    /// <summary>
    /// Probes a file. Never throws for a file FFprobe dislikes — an unreadable file comes
    /// back as <see cref="MediaProbe.Unknown"/> and the conversion is left to fail with
    /// FFmpeg's own, better, error message.
    /// </summary>
    public async Task<MediaProbe> ReadAsync(string ffprobePath, string inputPath, CancellationToken cancellationToken)
    {
        var result = await _processRunner.RunAsync(
            new ProcessRequest
            {
                FileName = ffprobePath,
                Arguments = BuildArguments(inputPath),
            },
            cancellationToken).ConfigureAwait(false);

        return result.IsSuccess ? Parse(result.StandardOutput) : MediaProbe.Unknown;
    }

    internal static IReadOnlyList<string> BuildArguments(string inputPath) => new[]
    {
        "-v", "error",
        "-hide_banner",
        "-print_format", "json",
        "-show_format",
        "-show_streams",
        inputPath,
    };

    internal static MediaProbe Parse(string? json)
    {
        if (string.IsNullOrWhiteSpace(json))
        {
            return MediaProbe.Unknown;
        }

        try
        {
            using var document = JsonDocument.Parse(json);
            var root = document.RootElement;

            return new MediaProbe(
                ReadDuration(root),
                ReadStreamString(root, "video", "codec_name"),
                ReadStreamString(root, "audio", "codec_name"),
                ReadStreamInt(root, "video", "width"),
                ReadStreamInt(root, "video", "height"));
        }
        catch (JsonException)
        {
            return MediaProbe.Unknown;
        }
    }

    private static TimeSpan? ReadDuration(JsonElement root)
    {
        if (!root.TryGetProperty("format", out var format)
            || !format.TryGetProperty("duration", out var duration))
        {
            return null;
        }

        var text = duration.ValueKind == JsonValueKind.String ? duration.GetString() : duration.ToString();

        return double.TryParse(text, NumberStyles.Float, CultureInfo.InvariantCulture, out var seconds) && seconds > 0
            ? TimeSpan.FromSeconds(seconds)
            : null;
    }

    private static JsonElement? FindStream(JsonElement root, string codecType)
    {
        if (!root.TryGetProperty("streams", out var streams) || streams.ValueKind != JsonValueKind.Array)
        {
            return null;
        }

        foreach (var stream in streams.EnumerateArray())
        {
            if (stream.TryGetProperty("codec_type", out var type)
                && string.Equals(type.GetString(), codecType, StringComparison.OrdinalIgnoreCase))
            {
                return stream;
            }
        }

        return null;
    }

    private static string? ReadStreamString(JsonElement root, string codecType, string property) =>
        FindStream(root, codecType) is { } stream
        && stream.TryGetProperty(property, out var value)
        && value.ValueKind == JsonValueKind.String
            ? value.GetString()
            : null;

    private static int? ReadStreamInt(JsonElement root, string codecType, string property) =>
        FindStream(root, codecType) is { } stream
        && stream.TryGetProperty(property, out var value)
        && value.TryGetInt32(out var number)
            ? number
            : null;
}
