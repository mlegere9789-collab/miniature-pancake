namespace MediaSuite.Core.Tooling;

/// <summary>What a finished child process left behind.</summary>
/// <param name="ExitCode">Process exit code; 0 means success for every tool we run.</param>
/// <param name="StandardOutput">Captured stdout, trimmed to the tail if the tool was chatty.</param>
/// <param name="StandardError">Captured stderr, trimmed to the tail.</param>
/// <param name="Duration">Wall-clock time the process ran for.</param>
public sealed record ProcessResult(
    int ExitCode,
    string StandardOutput,
    string StandardError,
    TimeSpan Duration)
{
    public bool IsSuccess => ExitCode == 0;

    /// <summary>
    /// Best guess at why a tool failed: its last stderr line, falling back to stdout and
    /// then to the exit code. Tools tend to put the real reason on the final line.
    /// </summary>
    public string DescribeFailure()
    {
        var lastError = LastMeaningfulLine(StandardError) ?? LastMeaningfulLine(StandardOutput);
        return lastError is null
            ? $"the tool exited with code {ExitCode}"
            : lastError;
    }

    private static string? LastMeaningfulLine(string text)
    {
        if (string.IsNullOrWhiteSpace(text))
        {
            return null;
        }

        var lines = text.Split('\n', StringSplitOptions.RemoveEmptyEntries | StringSplitOptions.TrimEntries);
        return lines.Length == 0 ? null : lines[^1];
    }
}
