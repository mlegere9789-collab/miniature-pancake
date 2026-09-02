namespace MediaSuite.Core.Formats;

/// <summary>
/// A single file format the app knows about.
/// </summary>
/// <param name="Extension">Canonical extension, lower case, no leading dot (e.g. "jpg").</param>
/// <param name="DisplayName">Human readable name shown in pickers.</param>
/// <param name="Kind">Family the format belongs to.</param>
/// <param name="CanRead">True when the app can use this format as an input.</param>
/// <param name="CanWrite">True when the app can produce this format as an output.</param>
public sealed record FileFormat(
    string Extension,
    string DisplayName,
    MediaKind Kind,
    bool CanRead = true,
    bool CanWrite = true)
{
    /// <summary>Alternative extensions that map to the same format (e.g. "jpeg" for "jpg").</summary>
    public IReadOnlyList<string> Aliases { get; init; } = Array.Empty<string>();

    /// <summary>All extensions that resolve to this format, canonical one first.</summary>
    public IEnumerable<string> AllExtensions => new[] { Extension }.Concat(Aliases);
}
