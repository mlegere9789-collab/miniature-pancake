namespace MediaSuite.Core.Jobs;

/// <summary>Outcome of one job run.</summary>
public sealed record JobResult
{
    public required JobStatus Status { get; init; }

    /// <summary>Files that were produced. Empty when the job failed or was canceled.</summary>
    public IReadOnlyList<string> OutputPaths { get; init; } = Array.Empty<string>();

    /// <summary>How long the run took.</summary>
    public TimeSpan Duration { get; init; }

    /// <summary>User-facing failure message. Null on success.</summary>
    public string? ErrorMessage { get; init; }

    /// <summary>
    /// Set when the local output succeeded but the optional Google Drive upload the job
    /// asked for did not — the converted files still exist locally either way, so this is
    /// a warning on an otherwise completed job rather than a failure.
    /// </summary>
    public string? UploadWarning { get; init; }

    /// <summary>
    /// Raw tool output kept for diagnostics — the tail of stderr from the underlying
    /// process, so a failed job can explain itself without a log panel.
    /// </summary>
    public string? Diagnostics { get; init; }

    public bool IsSuccess => Status == JobStatus.Completed;

    public static JobResult Success(IReadOnlyList<string> outputs, TimeSpan duration) =>
        new() { Status = JobStatus.Completed, OutputPaths = outputs, Duration = duration };

    public static JobResult Failure(string message, TimeSpan duration = default, string? diagnostics = null) =>
        new() { Status = JobStatus.Failed, ErrorMessage = message, Duration = duration, Diagnostics = diagnostics };

    public static JobResult Canceled(TimeSpan duration = default) =>
        new() { Status = JobStatus.Canceled, Duration = duration };
}
