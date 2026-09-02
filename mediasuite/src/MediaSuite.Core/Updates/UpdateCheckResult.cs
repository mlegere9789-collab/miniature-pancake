namespace MediaSuite.Core.Updates;

/// <summary>
/// Outcome of one update check. Never triggers anything on its own — the app only ever
/// offers the download page and lets the user decide, per the brief's "never silent".
/// </summary>
public sealed record UpdateCheckResult
{
    public required bool HasUpdate { get; init; }

    /// <summary>The running app's own version, formatted for display.</summary>
    public required string CurrentVersion { get; init; }

    /// <summary>The latest published version's tag, as GitHub reports it (e.g. "v0.2.0").</summary>
    public string? LatestVersion { get; init; }

    /// <summary>Where the user goes to download it. Null when there is nothing to offer.</summary>
    public string? DownloadUrl { get; init; }

    /// <summary>
    /// Set when the check itself failed (offline, GitHub unreachable, an unexpected
    /// response) rather than succeeding with no update. Never shown as an error dialog —
    /// a failed check just means the banner stays hidden.
    /// </summary>
    public string? ErrorMessage { get; init; }
}
