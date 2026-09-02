namespace MediaSuite.Core.Jobs;

/// <summary>
/// One progress tick from an engine.
/// </summary>
/// <param name="Percent">0-100 for the job as a whole; null when the engine cannot estimate it.</param>
/// <param name="Stage">Short description of what is happening ("Encoding", "Upscaling", "Uploading").</param>
/// <param name="CurrentItem">File currently being processed, for batch jobs.</param>
public readonly record struct JobProgress(double? Percent, string? Stage = null, string? CurrentItem = null)
{
    public static JobProgress At(double percent, string? stage = null) =>
        new(Math.Clamp(percent, 0d, 100d), stage);

    /// <summary>Progress tick with no percentage — the UI shows an indeterminate bar.</summary>
    public static JobProgress Indeterminate(string stage) => new(null, stage);
}
