using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;
using Xunit;

namespace MediaSuite.Core.Tests;

public class JobQueueManagerTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);

    public void Dispose() => _temp.Dispose();

    private JobQueueManager CreateQueue(
        EngineRegistry registry,
        int maxConcurrency = 4,
        ToolLocator? toolLocator = null) =>
        new(registry, new DiskTempWorkspaceFactory(_temp.Combine("work")), maxConcurrency, toolLocator);

    private static JobSpec Spec(string operationId = "image.convert") => new()
    {
        OperationId = operationId,
        InputPaths = new[] { "input.png" },
        Output = new OutputTarget { Directory = "out", Format = "jpg" },
    };

    private static async Task WaitForIdle(JobQueueManager queue) =>
        await queue.WaitForIdleAsync().WaitAsync(Timeout);

    [Fact]
    public async Task A_queued_job_runs_and_completes()
    {
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Instant()));

        var job = queue.Enqueue(Spec());
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.True(job.IsFinished);
        Assert.Equal(100, job.PercentComplete);
        Assert.NotNull(job.StartedAt);
        Assert.NotNull(job.FinishedAt);
    }

    [Fact]
    public async Task Progress_ticks_reach_the_job_and_raise_an_event()
    {
        var engine = new FakeEngine((_, progress, _) =>
        {
            progress.Report(JobProgress.At(40, "Encoding"));
            progress.Report(new JobProgress(80, "Encoding", "clip.mp4"));
            return Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero));
        });

        using var queue = CreateQueue(new EngineRegistry().Register(engine));

        var ticks = 0;
        queue.JobProgressChanged += (_, _) => Interlocked.Increment(ref ticks);

        var job = queue.Enqueue(Spec());
        await WaitForIdle(queue);

        Assert.Equal(2, ticks);
        Assert.Equal("Encoding", job.Stage);
        Assert.Equal("clip.mp4", job.CurrentItem);
    }

    [Fact]
    public async Task Never_runs_more_jobs_at_once_than_the_concurrency_limit()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var engine = FakeEngine.Gated(gate);
        using var queue = CreateQueue(new EngineRegistry().Register(engine), maxConcurrency: 2);

        for (var i = 0; i < 8; i++)
        {
            queue.Enqueue(Spec());
        }

        await WaitUntil(() => queue.RunningCount == 2);
        Assert.Equal(6, queue.PendingCount);

        // Wait for the engines themselves to be in flight: the queue books a job as
        // running the moment it hands it to the thread pool, which is earlier.
        await WaitUntil(() => engine.ActiveCount == 2);

        gate.SetResult();
        await WaitForIdle(queue);

        Assert.Equal(2, engine.PeakActive);
        Assert.Equal(8, queue.Jobs.Count(job => job.Status == JobStatus.Completed));
    }

    [Fact]
    public async Task Raising_the_concurrency_limit_starts_waiting_jobs_immediately()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 1);

        for (var i = 0; i < 4; i++)
        {
            queue.Enqueue(Spec());
        }

        await WaitUntil(() => queue.RunningCount == 1);

        queue.MaxConcurrency = 4;
        await WaitUntil(() => queue.RunningCount == 4);

        gate.SetResult();
        await WaitForIdle(queue);
    }

    [Fact]
    public async Task One_scheduling_pass_fills_every_free_slot()
    {
        // Regression guard: the scheduler used to count each job it was starting twice,
        // so a queue set to 4 would only ever run 2. Pausing first forces the whole batch
        // to be scheduled in a single pass, which is where the miscount showed up.
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var engine = FakeEngine.Gated(gate);
        using var queue = CreateQueue(new EngineRegistry().Register(engine), maxConcurrency: 4);

        queue.Pause();
        for (var i = 0; i < 6; i++)
        {
            queue.Enqueue(Spec());
        }

        Assert.Equal(0, queue.RunningCount);

        queue.Resume();

        Assert.Equal(4, queue.RunningCount);
        Assert.Equal(2, queue.PendingCount);

        await WaitUntil(() => engine.ActiveCount == 4);
        Assert.Equal(4, engine.PeakActive);

        gate.SetResult();
        await WaitForIdle(queue);
    }

    [Fact]
    public async Task Lowering_the_limit_leaves_running_jobs_alone()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 4);

        for (var i = 0; i < 4; i++)
        {
            queue.Enqueue(Spec());
        }

        await WaitUntil(() => queue.RunningCount == 4);
        queue.MaxConcurrency = 1;

        Assert.Equal(4, queue.RunningCount);

        gate.SetResult();
        await WaitForIdle(queue);
        Assert.Equal(4, queue.Jobs.Count(job => job.Status == JobStatus.Completed));
    }

    [Fact]
    public async Task Pausing_stops_new_jobs_starting_but_lets_running_ones_finish()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 1);

        var first = queue.Enqueue(Spec());
        await WaitUntil(() => queue.RunningCount == 1);

        queue.Pause();
        var second = queue.Enqueue(Spec());

        gate.SetResult();
        await WaitUntil(() => first.Status == JobStatus.Completed);

        Assert.True(queue.IsPaused);
        Assert.Equal(JobStatus.Pending, second.Status);
        Assert.Equal(1, queue.PendingCount);

        queue.Resume();
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, second.Status);
    }

    [Fact]
    public async Task Cancelling_a_running_job_stops_it_and_leaves_the_others_running()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 2);

        var doomed = queue.Enqueue(Spec());
        var survivor = queue.Enqueue(Spec());
        await WaitUntil(() => queue.RunningCount == 2);

        queue.Cancel(doomed);
        await WaitUntil(() => doomed.Status == JobStatus.Canceled);

        Assert.Equal(JobStatus.Running, survivor.Status);

        gate.SetResult();
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, survivor.Status);
    }

    [Fact]
    public async Task Cancelling_a_waiting_job_means_it_never_starts()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var engine = FakeEngine.Gated(gate);
        using var queue = CreateQueue(new EngineRegistry().Register(engine), maxConcurrency: 1);

        var running = queue.Enqueue(Spec());
        var waiting = queue.Enqueue(Spec());
        await WaitUntil(() => queue.RunningCount == 1);

        queue.Cancel(waiting);
        Assert.Equal(JobStatus.Canceled, waiting.Status);

        gate.SetResult();
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, running.Status);
        Assert.Equal(1, engine.PeakActive);
        Assert.Single(engine.ObservedWorkingDirectories);
    }

    [Fact]
    public async Task CancelAll_stops_running_and_waiting_jobs_and_the_queue_goes_idle()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 2);

        for (var i = 0; i < 6; i++)
        {
            queue.Enqueue(Spec());
        }

        await WaitUntil(() => queue.RunningCount == 2);
        queue.CancelAll();
        await WaitForIdle(queue);

        Assert.All(queue.Jobs, job => Assert.Equal(JobStatus.Canceled, job.Status));
        Assert.True(queue.IsIdle);
    }

    [Fact]
    public async Task Cancelling_everything_while_paused_still_drains_the_queue()
    {
        // Regression guard: canceled jobs used to stay queued, so a paused queue could
        // never report itself idle again.
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Instant()));

        queue.Pause();
        queue.Enqueue(Spec());
        queue.Enqueue(Spec());

        queue.CancelAll();
        await WaitForIdle(queue);

        Assert.True(queue.IsIdle);
        Assert.Equal(0, queue.PendingCount);
    }

    [Fact]
    public async Task An_engine_that_fails_records_the_reason_without_touching_other_jobs()
    {
        var registry = new EngineRegistry()
            .Register(new FakeEngine(
                (_, _, _) => Task.FromResult(JobResult.Failure("codec not supported")),
                handles: "video.convert"))
            .Register(FakeEngine.Instant(id: "ok", handles: "image.convert"));

        using var queue = CreateQueue(registry);

        var failing = queue.Enqueue(Spec("video.convert"));
        var fine = queue.Enqueue(Spec("image.convert"));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Failed, failing.Status);
        Assert.Equal("codec not supported", failing.ErrorMessage);
        Assert.Equal(JobStatus.Completed, fine.Status);
    }

    [Fact]
    public async Task An_engine_that_throws_fails_only_its_own_job()
    {
        var registry = new EngineRegistry()
            .Register(new FakeEngine(
                (_, _, _) => throw new InvalidOperationException("engine blew up"),
                handles: "video.convert"))
            .Register(FakeEngine.Instant(id: "ok", handles: "image.convert"));

        using var queue = CreateQueue(registry);

        var thrown = queue.Enqueue(Spec("video.convert"));
        var fine = queue.Enqueue(Spec("image.convert"));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Failed, thrown.Status);
        Assert.Contains("engine blew up", thrown.ErrorMessage!, StringComparison.Ordinal);
        Assert.Equal(JobStatus.Completed, fine.Status);
    }

    [Fact]
    public async Task A_job_with_no_engine_fails_and_names_the_operation()
    {
        using var queue = CreateQueue(new EngineRegistry());

        var job = queue.Enqueue(Spec("pdf.merge"));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Failed, job.Status);
        Assert.Contains("pdf.merge", job.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task A_job_whose_tool_is_not_installed_fails_before_running_and_names_the_tool()
    {
        var engine = FakeEngine.Instant();
        var withTools = new FakeEngine(
            (_, _, _) => Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero)),
            id: "needs-ffmpeg",
            requiredTools: new[] { ExternalToolId.FFmpeg });

        var locator = new ToolLocator(new[] { _temp.Combine("no-tools") }, pathVariable: string.Empty);
        using var queue = CreateQueue(new EngineRegistry().Register(engine).Register(withTools), toolLocator: locator);

        var job = queue.Enqueue(Spec());
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Failed, job.Status);
        Assert.Contains("FFmpeg", job.ErrorMessage!, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Each_job_gets_its_own_scratch_folder_which_is_removed_afterwards()
    {
        string? observed = null;
        var engine = new FakeEngine((spec, _, _) =>
        {
            observed = spec.WorkingDirectory;
            Assert.NotNull(observed);
            Assert.True(Directory.Exists(observed), "the workspace should exist while the engine runs");
            File.WriteAllText(Path.Combine(observed!, "intermediate.tmp"), "scratch");
            return Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero));
        });

        using var queue = CreateQueue(new EngineRegistry().Register(engine));

        queue.Enqueue(Spec());
        await WaitForIdle(queue);

        Assert.NotNull(observed);
        Assert.False(Directory.Exists(observed), "the workspace should be gone once the job finished");
    }

    [Fact]
    public async Task Scratch_folders_are_unique_per_job()
    {
        var engine = FakeEngine.Instant();
        using var queue = CreateQueue(new EngineRegistry().Register(engine), maxConcurrency: 1);

        for (var i = 0; i < 5; i++)
        {
            queue.Enqueue(Spec());
        }

        await WaitForIdle(queue);

        Assert.Equal(5, engine.ObservedWorkingDirectories.Distinct(StringComparer.OrdinalIgnoreCase).Count());
    }

    [Fact]
    public async Task QueueIdle_is_raised_once_the_last_job_finishes()
    {
        using var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Instant()));

        var idleRaised = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        queue.QueueIdle += (_, _) => idleRaised.TrySetResult();

        queue.Enqueue(Spec());

        await idleRaised.Task.WaitAsync(Timeout);
        Assert.True(queue.IsIdle);
    }

    [Fact]
    public async Task Jobs_start_in_the_order_they_were_added()
    {
        var started = new List<string>();
        var engine = new FakeEngine((spec, _, _) =>
        {
            lock (started)
            {
                started.Add(spec.GetOption("tag", "?"));
            }

            return Task.FromResult(JobResult.Success(Array.Empty<string>(), TimeSpan.Zero));
        });

        using var queue = CreateQueue(new EngineRegistry().Register(engine), maxConcurrency: 1);

        foreach (var tag in new[] { "a", "b", "c", "d" })
        {
            queue.Enqueue(Spec().WithOption("tag", tag));
        }

        await WaitForIdle(queue);

        Assert.Equal(new[] { "a", "b", "c", "d" }, started);
    }

    [Fact]
    public async Task Disposing_the_queue_cancels_work_still_in_flight()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var queue = CreateQueue(new EngineRegistry().Register(FakeEngine.Gated(gate)), maxConcurrency: 1);

        var job = queue.Enqueue(Spec());
        await WaitUntil(() => queue.RunningCount == 1);

        queue.Dispose();

        await WaitUntil(() => job.Status == JobStatus.Canceled);
        Assert.Equal(JobStatus.Canceled, job.Status);
    }

    [Fact]
    public async Task An_empty_queue_is_already_idle()
    {
        using var queue = CreateQueue(new EngineRegistry());

        Assert.True(queue.IsIdle);
        await queue.WaitForIdleAsync().WaitAsync(Timeout);
    }

    /// <summary>Polls a condition instead of sleeping a fixed amount, to keep CI stable.</summary>
    private static async Task WaitUntil(Func<bool> condition)
    {
        var deadline = DateTime.UtcNow + Timeout;

        while (DateTime.UtcNow < deadline)
        {
            if (condition())
            {
                return;
            }

            await Task.Delay(10);
        }

        Assert.Fail("Timed out waiting for the queue to reach the expected state.");
    }
}
