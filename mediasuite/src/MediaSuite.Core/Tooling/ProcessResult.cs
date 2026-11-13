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

    /// <summary>
    /// Everything captured from the run, for a "Copy details" button — the exit code plus
    /// the full stderr and stdout, not just <see cref="DescribeFailure"/>'s single summary
    /// line. Tools often bury the actual cause several lines before the final one.
    /// </summary>
    public string FullOutput()
    {
        var sections = new List<string> { $"Exit code: {ExitCode}" };

        if (!string.IsNullOrWhiteSpace(StandardError))
        {
            sections.Add("--- stderr ---" + Environment.NewLine + StandardError.Trim());
        }

        if (!string.IsNullOrWhiteSpace(StandardOutput))
        {
            sections.Add("--- stdout ---" + Environment.NewLine + StandardOutput.Trim());
        }

        return string.Join(Environment.NewLine + Environment.NewLine, sections);
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
