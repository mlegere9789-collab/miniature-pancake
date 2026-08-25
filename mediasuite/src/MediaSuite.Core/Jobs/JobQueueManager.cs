using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Jobs;

/// <summary>
/// Runs queued jobs across several threads, engine-agnostically.
/// </summary>
/// <remarks>
/// <para>
/// There is no background pump loop. Scheduling happens on the events that can possibly
/// free or create capacity — enqueue, completion, resume, a concurrency change — so the
/// queue costs nothing while idle and there is no polling interval to tune.
/// </para>
/// <para>
/// Pausing stops <em>starting</em> work; jobs already running are left to finish, since
/// killing a half-written encode to honour a pause would only cost the user the work.
/// Use <see cref="CancelAll"/> to stop everything.
/// </para>
/// </remarks>
public sealed class JobQueueManager : IDisposable
{
    private readonly EngineRegistry _engines;
    private readonly ITempWorkspaceFactory _workspaces;
    private readonly ToolLocator? _toolLocator;
    private readonly TimeProvider _time;

    private readonly object _gate = new();
    private readonly List<QueuedJob> _jobs = new();
    private readonly Queue<QueuedJob> _pending = new();
    private readonly Dictionary<Guid, CancellationTokenSource> _running = new();
    private readonly CancellationTokenSource _shutdown = new();

    private TaskCompletionSource _idle = CompletedIdleSource();
    private int _maxConcurrency;
    private bool _paused;
    private bool _disposed;

    public JobQueueManager(
        EngineRegistry engines,
        ITempWorkspaceFactory workspaces,
        int maxConcurrency,
        ToolLocator? toolLocator = null,
        TimeProvider? timeProvider = null)
    {
        _engines = engines ?? throw new ArgumentNullException(nameof(engines));
        _workspaces = workspaces ?? throw new ArgumentNullException(nameof(workspaces));
        _toolLocator = toolLocator;
        _time = timeProvider ?? TimeProvider.System;
        _maxConcurrency = Math.Max(1, maxConcurrency);
    }

    /// <summary>Raised when a job joins the queue.</summary>
    public event EventHandler<QueuedJob>? JobAdded;

    /// <summary>Raised when a job starts, finishes, or is canceled.</summary>
    public event EventHandler<QueuedJob>? JobStatusChanged;

    /// <summary>Raised on every progress tick from an engine.</summary>
    public event EventHandler<QueuedJob>? JobProgressChanged;

    /// <summary>Raised when the last active job finishes and nothing is pending.</summary>
    public event EventHandler? QueueIdle;

    /// <summary>
    /// How many jobs may run at once. Defaults to the CPU core count and is overridable
    /// from Settings; lowering it never interrupts jobs already running.
    /// </summary>
    public int MaxConcurrency
    {
        get
        {
            lock (_gate)
            {
                return _maxConcurrency;
            }
        }
        set
        {
            lock (_gate)
            {
                var clamped = Math.Max(1, value);
                if (_maxConcurrency == clamped)
                {
                    return;
                }

                _maxConcurrency = clamped;
            }

            Schedule();
        }
    }

    /// <summary>True while the queue is not starting new work.</summary>
    public bool IsPaused
    {
        get
        {
            lock (_gate)
            {
                return _paused;
            }
        }
    }

    /// <summary>Every job this session, finished ones included.</summary>
    public IReadOnlyList<QueuedJob> Jobs
    {
        get
        {
            lock (_gate)
            {
                return _jobs.ToList();
            }
        }
    }

    /// <summary>Jobs currently running.</summary>
    public int RunningCount
    {
        get
        {
            lock (_gate)
            {
                return _running.Count;
            }
        }
    }

    /// <summary>Jobs waiting for a slot.</summary>
    public int PendingCount
    {
        get
        {
            lock (_gate)
            {
                return _pending.Count;
            }
        }
    }

    /// <summary>True when nothing is running and nothing is waiting.</summary>
    public bool IsIdle
    {
        get
        {
            lock (_gate)
            {
                return _running.Count == 0 && _pending.Count == 0;
            }
        }
    }

    /// <summary>Adds a job and starts it as soon as there is a free slot.</summary>
    public QueuedJob Enqueue(JobSpec spec)
    {
        ArgumentNullException.ThrowIfNull(spec);
        ObjectDisposedException.ThrowIf(_disposed, this);

        var job = new QueuedJob(spec, _time.GetUtcNow());

        lock (_gate)
        {
            _jobs.Add(job);
            _pending.Enqueue(job);

            if (_idle.Task.IsCompleted)
            {
                _idle = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
            }
        }

        JobAdded?.Invoke(this, job);
        Schedule();
        return job;
    }

    /// <summary>Adds several jobs, preserving order.</summary>
    public IReadOnlyList<QueuedJob> EnqueueRange(IEnumerable<JobSpec> specs)
    {
        ArgumentNullException.ThrowIfNull(specs);
        return specs.Select(Enqueue).ToList();
    }

    /// <summary>
    /// Cancels one job. A pending job never starts; a running one has its token canceled
    /// and the engine is expected to stop promptly.
    /// </summary>
    public void Cancel(QueuedJob job)
    {
        ArgumentNullException.ThrowIfNull(job);

        CancellationTokenSource? cts;
        var canceledWhilePending = false;

        lock (_gate)
        {
            if (job.IsFinished)
            {
                return;
            }

            if (_running.TryGetValue(job.Id, out cts))
            {
                // Let the running engine unwind; RunJobAsync records the outcome.
            }
            else
            {
                canceledWhilePending = true;
            }
        }

        if (canceledWhilePending)
        {
            // Stays in _pending; Schedule skips finished jobs when it reaches it.
            Finish(job, JobResult.Canceled());
            Schedule();
            return;
        }

        TryCancel(cts);
    }

    /// <summary>
    /// Cancels a token source that the job's own finally block may already have disposed.
    /// </summary>
    private static void TryCancel(CancellationTokenSource? cancellation)
    {
        try
        {
            cancellation?.Cancel();
        }
        catch (ObjectDisposedException)
        {
            // The job finished on its own between us taking the reference and cancelling.
        }
    }

    /// <summary>Cancels everything — running and waiting alike.</summary>
    public void CancelAll()
    {
        List<QueuedJob> pending;
        List<CancellationTokenSource> running;

        lock (_gate)
        {
            pending = _pending.Where(job => !job.IsFinished).ToList();
            running = _running.Values.ToList();
        }

        foreach (var job in pending)
        {
            Finish(job, JobResult.Canceled());
        }

        foreach (var cts in running)
        {
            TryCancel(cts);
        }

        Schedule();
    }

    /// <summary>Stops starting new jobs. Jobs already running carry on.</summary>
    public void Pause()
    {
        lock (_gate)
        {
            _paused = true;
        }
    }

    /// <summary>Starts filling free slots again.</summary>
    public void Resume()
    {
        lock (_gate)
        {
            if (!_paused)
            {
                return;
            }

            _paused = false;
        }

        Schedule();
    }

    /// <summary>Completes once the queue has nothing running and nothing pending.</summary>
    public Task WaitForIdleAsync()
    {
        lock (_gate)
        {
            return _running.Count == 0 && _pending.Count == 0 ? Task.CompletedTask : _idle.Task;
        }
    }

    /// <summary>Starts as many pending jobs as the current concurrency limit allows.</summary>
    private void Schedule()
    {
        List<(QueuedJob Job, CancellationTokenSource Cancellation)> starting = new();

        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            DropFinishedFromPending();

            while (!_paused && _running.Count + starting.Count < _maxConcurrency && _pending.Count > 0)
            {
                var job = _pending.Dequeue();

                // Canceled while it sat in the queue.
                if (job.IsFinished)
                {
                    continue;
                }

                var cancellation = CancellationTokenSource.CreateLinkedTokenSource(_shutdown.Token);
                _running[job.Id] = cancellation;
                starting.Add((job, cancellation));
            }
        }

        foreach (var (job, cancellation) in starting)
        {
            _ = Task.Run(() => RunJobAsync(job, cancellation));
        }

        RaiseIdleIfDrained();
    }

    /// <summary>
    /// Removes jobs canceled while they waited. Without this they would sit in the queue
    /// holding it "not idle" forever whenever the queue is paused or already full.
    /// Caller must hold <see cref="_gate"/>.
    /// </summary>
    private void DropFinishedFromPending()
    {
        if (!_pending.Any(job => job.IsFinished))
        {
            return;
        }

        var survivors = _pending.Where(job => !job.IsFinished).ToList();
        _pending.Clear();

        foreach (var job in survivors)
        {
            _pending.Enqueue(job);
        }
    }

    private async Task RunJobAsync(QueuedJob job, CancellationTokenSource cancellation)
    {
        var startedAt = _time.GetUtcNow();
        TempWorkspace? workspace = null;

        try
        {
            var engine = _engines.Resolve(job.Spec);
            if (engine is null)
            {
                Finish(job, JobResult.Failure(
                    $"Nothing can handle '{job.Spec.OperationId}' yet — no engine is registered for it."));
                return;
            }

            var missing = MissingTools(engine);
            if (missing.Count > 0)
            {
                Finish(job, JobResult.Failure(
                    $"{engine.DisplayName} needs {string.Join(", ", missing)}, which is not installed. "
                    + "Settings has the download links and shows where the app looks."));
                return;
            }

            cancellation.Token.ThrowIfCancellationRequested();

            workspace = _workspaces.Create(job.Id);
            job.MarkRunning(startedAt);
            JobStatusChanged?.Invoke(this, job);

            var spec = job.Spec with { WorkingDirectory = workspace.Path };
            var progress = new DelegateProgress<JobProgress>(tick =>
            {
                job.ReportProgress(tick);
                JobProgressChanged?.Invoke(this, job);
            });

            var result = await engine.RunAsync(spec, progress, cancellation.Token).ConfigureAwait(false);
            Finish(job, WithDuration(result, startedAt));
        }
        catch (OperationCanceledException)
        {
            Finish(job, JobResult.Canceled(_time.GetUtcNow() - startedAt));
        }
        catch (Exception ex)
        {
            // An engine that throws is a bug in that engine, not a reason to take the
            // queue down: record it against the job and keep the other jobs running.
            Finish(job, JobResult.Failure(ex.Message, _time.GetUtcNow() - startedAt, ex.ToString()));
        }
        finally
        {
            workspace?.Dispose();

            lock (_gate)
            {
                _running.Remove(job.Id);
                cancellation.Dispose();
            }

            Schedule();
        }
    }

    /// <summary>Records a job's outcome exactly once, whichever path got there first.</summary>
    private void Finish(QueuedJob job, JobResult result)
    {
        // Claimed with an interlocked flag rather than under _gate: the notification this
        // raises runs subscriber code, which must never happen while holding the lock.
        if (!job.TryClaimCompletion())
        {
            return;
        }

        job.Finish(result, _time.GetUtcNow());
        JobStatusChanged?.Invoke(this, job);
    }

    private JobResult WithDuration(JobResult result, DateTimeOffset startedAt) =>
        result.Duration == TimeSpan.Zero
            ? result with { Duration = _time.GetUtcNow() - startedAt }
            : result;

    private IReadOnlyList<string> MissingTools(IConversionEngine engine)
    {
        if (_toolLocator is null || engine.RequiredTools.Count == 0)
        {
            return Array.Empty<string>();
        }

        return engine.RequiredTools
            .Where(tool => !_toolLocator.Locate(tool).Found)
            .Select(tool => ToolManifest.Get(tool).DisplayName)
            .Distinct(StringComparer.Ordinal)
            .ToList();
    }

    private void RaiseIdleIfDrained()
    {
        TaskCompletionSource? toComplete = null;

        lock (_gate)
        {
            if (_running.Count == 0 && _pending.Count == 0 && !_idle.Task.IsCompleted)
            {
                toComplete = _idle;
            }
        }

        if (toComplete is null)
        {
            return;
        }

        toComplete.TrySetResult();
        QueueIdle?.Invoke(this, EventArgs.Empty);
    }

    private static TaskCompletionSource CompletedIdleSource()
    {
        var source = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        source.TrySetResult();
        return source;
    }

    public void Dispose()
    {
        lock (_gate)
        {
            if (_disposed)
            {
                return;
            }

            _disposed = true;
        }

        _shutdown.Cancel();
        _shutdown.Dispose();
    }
}
