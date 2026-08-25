using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Tests;

/// <summary>
/// Test engine whose completion is driven by the test rather than by a timer, so the
/// concurrency and cancellation tests are deterministic instead of sleep-based.
/// </summary>
public sealed class FakeEngine : IConversionEngine
{
    private readonly Func<JobSpec, IProgress<JobProgress>, CancellationToken, Task<JobResult>> _behaviour;

    public FakeEngine(
        Func<JobSpec, IProgress<JobProgress>, CancellationToken, Task<JobResult>> behaviour,
        string id = "fake",
        string? handles = null,
        IReadOnlyList<ExternalToolId>? requiredTools = null)
    {
        _behaviour = behaviour;
        Id = id;
        Handles = handles;
        RequiredTools = requiredTools ?? Array.Empty<ExternalToolId>();
    }

    public string Id { get; }

    public string DisplayName => Id;

    public IReadOnlyList<ExternalToolId> RequiredTools { get; }

    /// <summary>Operation this engine claims; null means "anything".</summary>
    public string? Handles { get; }

    /// <summary>Working directories the queue handed to each run, in start order.</summary>
    public List<string?> ObservedWorkingDirectories { get; } = new();

    private readonly object _gate = new();
    private int _active;
    private int _peakActive;

    /// <summary>The highest number of runs ever in flight at the same time.</summary>
    public int PeakActive
    {
        get
        {
            lock (_gate)
            {
                return _peakActive;
            }
        }
    }

    public bool CanHandle(JobSpec spec) =>
        Handles is null || string.Equals(spec.OperationId, Handles, StringComparison.OrdinalIgnoreCase);

    public async Task<JobResult> RunAsync(
        JobSpec spec,
        IProgress<JobProgress> progress,
        CancellationToken cancellationToken)
    {
        lock (_gate)
        {
            ObservedWorkingDirectories.Add(spec.WorkingDirectory);
            _active++;
            _peakActive = Math.Max(_peakActive, _active);
        }

        try
        {
            return await _behaviour(spec, progress, cancellationToken).ConfigureAwait(false);
        }
        finally
        {
            lock (_gate)
            {
                _active--;
            }
        }
    }

    /// <summary>Engine that finishes immediately.</summary>
    public static FakeEngine Instant(string id = "fake", string? handles = null) =>
        new((_, _, _) => Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero)),
            id, handles);

    /// <summary>Engine that blocks until the returned gate is released.</summary>
    public static FakeEngine Gated(TaskCompletionSource gate, string id = "fake", string? handles = null) =>
        new(async (_, _, token) =>
            {
                await gate.Task.WaitAsync(token).ConfigureAwait(false);
                return JobResult.Success(Array.Empty<string>(), TimeSpan.Zero);
            },
            id, handles);
}
