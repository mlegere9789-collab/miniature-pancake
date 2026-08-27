namespace MediaSuite.Core.Engines;

/// <summary>
/// What FFprobe could tell us about a file.
/// </summary>
/// <remarks>
/// Every field is optional. Probing is best-effort: a file we cannot read still converts,
/// it just shows indeterminate progress instead of a percentage.
/// </remarks>
public sealed record MediaProbe(
    TimeSpan? Duration = null,
    string? VideoCodec = null,
    string? AudioCodec = null,
    int? Width = null,
    int? Height = null)
{
    public static MediaProbe Unknown { get; } = new();

    public bool HasVideo => VideoCodec is { Length: > 0 };

    public bool HasAudio => AudioCodec is { Length: > 0 };
}
