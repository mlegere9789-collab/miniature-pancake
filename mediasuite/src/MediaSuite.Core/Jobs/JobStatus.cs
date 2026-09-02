namespace MediaSuite.Core.Jobs;

/// <summary>Lifecycle of a single job in the queue.</summary>
public enum JobStatus
{
    Pending,
    Running,
    Paused,
    Completed,
    Failed,
    Canceled,
}
