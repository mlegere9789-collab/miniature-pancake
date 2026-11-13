namespace MediaSuite.Core.Tooling;

/// <summary>Thrown when an external tool cannot be started or fails in a way the engine cannot interpret.</summary>
public sealed class ToolExecutionException : Exception
{
    public ToolExecutionException(string message, string? diagnostics = null)
        : base(message)
    {
        Diagnostics = diagnostics;
    }

    public ToolExecutionException(string message, Exception innerException)
        : base(message, innerException)
    {
    }

    /// <summary>
    /// The full captured process output behind <see cref="Exception.Message"/>'s one-line
    /// summary — null when the failure had no process run to show output for (a missing
    /// binary, a tool that exited 0 but wrote nothing). Carried through to
    /// <see cref="MediaSuite.Core.Jobs.JobResult.Diagnostics"/> so a failed job can
    /// explain itself in full without a log panel.
    /// </summary>
    public string? Diagnostics { get; }
}
