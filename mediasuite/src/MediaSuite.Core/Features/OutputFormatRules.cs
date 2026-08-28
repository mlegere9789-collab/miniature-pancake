using MediaSuite.Core.Engines;

namespace MediaSuite.Core.Features;

/// <summary>
/// What an operation implies about its output format, across every engine.
/// </summary>
/// <remarks>
/// One place for the UI to ask, so the format picker never needs to know which engine
/// will end up running the job. Each engine still owns its own table.
/// </remarks>
public static class OutputFormatRules
{
    /// <summary>
    /// Format the operation forces — "MP4 to MP3" can only write MP3 — or null when the
    /// user picks.
    /// </summary>
    public static string? ForcedFormat(string operationId) =>
        ImageOperations.FixedFormatFor(operationId)
        ?? FFmpegOperations.FixedFormatFor(operationId)
        ?? GifOperations.FixedFormatFor(operationId)
        ?? PdfOperations.FixedFormatFor(operationId);

    /// <summary>True for edit-in-place tools, which keep the input's format by default.</summary>
    public static bool KeepsSourceFormat(string operationId) =>
        ImageOperations.KeepsSourceFormat(operationId)
        || FFmpegOperations.KeepsSourceFormat(operationId);
}
