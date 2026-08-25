namespace MediaSuite.Core.Tooling;

/// <summary>Thrown when an external tool cannot be started or fails in a way the engine cannot interpret.</summary>
public sealed class ToolExecutionException : Exception
{
    public ToolExecutionException(string message)
        : base(message)
    {
    }

    public ToolExecutionException(string message, Exception innerException)
        : base(message, innerException)
    {
    }
}
