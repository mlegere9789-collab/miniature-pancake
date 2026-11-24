using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.App.Tests;

/// <summary>
/// Minimal stand-in engine for driving a real <see cref="JobQueueManager"/> from
/// <see cref="JobQueueViewModelTests"/>. Deliberately not shared with
/// MediaSuite.Core.Tests' own (richer) FakeEngine — that type lives in a test project,
/// not production code, so there is nothing for App.Tests to reference short of adding
/// an unusual test-project-to-test-project dependency for a handful of lines.
/// </summary>
public sealed class FakeEngine : IConversionEngine
{
    private readonly Func<JobSpec, IProgress<JobProgress>, CancellationToken, Task<JobResult>> _behaviour;
    private readonly Func<JobSpec, bool> _canHandle;

    private FakeEngine(
        Func<JobSpec, IProgress<JobProgress>, CancellationToken, Task<JobResult>> behaviour,
        Func<JobSpec, bool>? canHandle = null)
    {
        _behaviour = behaviour;
        _canHandle = canHandle ?? (_ => true);
    }

    public string Id => "fake";

    public string DisplayName => "Fake";

    public IReadOnlyList<ExternalToolId> RequiredTools => Array.Empty<ExternalToolId>();

    public bool CanHandle(JobSpec spec) => _canHandle(spec);

    public Task<JobResult> RunAsync(JobSpec spec, IProgress<JobProgress> progress, CancellationToken cancellationToken) =>
        _behaviour(spec, progress, cancellationToken);

    /// <summary>Engine that finishes immediately and claims every operation.</summary>
    public static FakeEngine Instant() =>
        new((_, _, _) => Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero)));

    /// <summary>Engine that blocks until the returned gate is released, honouring cancellation.</summary>
    public static FakeEngine Gated(TaskCompletionSource gate) =>
        new(async (_, _, token) =>
        {
            await gate.Task.WaitAsync(token).ConfigureAwait(false);
            return JobResult.Success(Array.Empty<string>(), TimeSpan.Zero);
        });

    /// <summary>
    /// Engine that finishes immediately but only claims the given operation ids — for
    /// tests that need some real-catalogue features to be "ready" and others not, the way
    /// only some engines are actually wired up in the real app.
    /// </summary>
    public static FakeEngine HandlesOnly(params string[] operationIds)
    {
        var claimed = new HashSet<string>(operationIds, StringComparer.OrdinalIgnoreCase);
        return new(
            (_, _, _) => Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero)),
            spec => claimed.Contains(spec.OperationId));
    }
}
