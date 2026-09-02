namespace MediaSuite.Core.Engines;

/// <summary>
/// The video and audio operations FFmpeg claims, and what each one implies about the
/// output format.
/// </summary>
public static class FFmpegOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "video.convert",
        "video.convert.mp4",
        "video.mov-to-mp4",
        "video.mp4-to-mp3",
        "video.to-mp3",
        "audio.convert",
        "audio.convert.mp3",
        "audio.mp3-to-ogg",
        "video.compress",
        "audio.compress.mp3",
        "audio.compress.wav",
        "video.crop",
        "video.trim",
    };

    /// <summary>Output format the operation forces, or null when the user chooses.</summary>
    public static string? FixedFormatFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "video.mp4-to-mp3" or "video.to-mp3" or "audio.convert.mp3" or "audio.compress.mp3" => "mp3",
        "video.convert.mp4" or "video.mov-to-mp4" => "mp4",
        "audio.mp3-to-ogg" => "ogg",
        "audio.compress.wav" => "wav",
        _ => null,
    };

    /// <summary>Operations that edit in place and keep the input's container.</summary>
    public static bool KeepsSourceFormat(string operationId) => operationId.ToLowerInvariant() switch
    {
        "video.compress" or "video.crop" or "video.trim" => true,
        _ => false,
    };

    /// <summary>True when the result has no video stream, so the video is dropped.</summary>
    public static bool ProducesAudioOnly(string operationId) => operationId.ToLowerInvariant() switch
    {
        "video.mp4-to-mp3" or "video.to-mp3" or "audio.convert" or "audio.convert.mp3"
            or "audio.mp3-to-ogg" or "audio.compress.mp3" or "audio.compress.wav" => true,
        _ => false,
    };
}
