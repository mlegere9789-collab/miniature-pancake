namespace MediaSuite.Core.Tooling;

/// <summary>One external tool invocation.</summary>
public sealed record ProcessRequest
{
    /// <summary>Full path to the executable. Resolved by <see cref="ToolLocator"/>, never a bare name.</summary>
    public required string FileName { get; init; }

    /// <summary>
    /// Arguments as separate values, never one joined string — the runner passes them
    /// through <c>ArgumentList</c>, so paths with spaces and quotes need no escaping.
    /// </summary>
    public required IReadOnlyList<string> Arguments { get; init; }

    public string? WorkingDirectory { get; init; }

    /// <summary>Called for each stdout line as it arrives, for progress parsing.</summary>
    public Action<string>? OnStandardOutputLine { get; init; }

    /// <summary>Called for each stderr line as it arrives. FFmpeg reports progress here.</summary>
    public Action<string>? OnStandardErrorLine { get; init; }

    /// <summary>
    /// How many trailing lines of each stream to keep. Bounded because tools like FFmpeg
    /// emit thousands of progress lines, and a long batch should not hold them all in memory.
    /// </summary>
    public int CapturedLineLimit { get; init; } = 60;
}
