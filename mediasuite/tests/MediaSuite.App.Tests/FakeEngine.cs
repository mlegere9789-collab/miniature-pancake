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

    private FakeEngine(Func<JobSpec, IProgress<JobProgress>, CancellationToken, Task<JobResult>> behaviour)
    {
        _behaviour = behaviour;
    }

    public string Id => "fake";

    public string DisplayName => "Fake";

    public IReadOnlyList<ExternalToolId> RequiredTools => Array.Empty<ExternalToolId>();

    public bool CanHandle(JobSpec spec) => true;

    public Task<JobResult> RunAsync(JobSpec spec, IProgress<JobProgress> progress, CancellationToken cancellationToken) =>
        _behaviour(spec, progress, cancellationToken);

    /// <summary>Engine that finishes immediately.</summary>
    public static FakeEngine Instant() =>
        new((_, _, _) => Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero)));

    /// <summary>Engine that blocks until the returned gate is released, honouring cancellation.</summary>
    public static FakeEngine Gated(TaskCompletionSource gate) =>
        new(async (_, _, token) =>
        {
            await gate.Task.WaitAsync(token).ConfigureAwait(false);
            return JobResult.Success(Array.Empty<string>(), TimeSpan.Zero);
        });
}
