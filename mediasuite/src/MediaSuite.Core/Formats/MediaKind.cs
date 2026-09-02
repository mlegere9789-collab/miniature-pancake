namespace MediaSuite.Core.Formats;

/// <summary>
/// Broad family a file belongs to. Used to route a job to the right engine and
/// to filter the format lists shown in the UI.
/// </summary>
public enum MediaKind
{
    Unknown = 0,
    Image,
    RawImage,
    Vector,
    Animation,
    Video,
    Audio,
    Document,
    Ebook,
    Pdf,
    Archive,
}
