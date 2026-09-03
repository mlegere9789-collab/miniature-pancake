using System.IO;
using System.Windows.Shell;
using System.Windows.Threading;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// <see cref="JobQueueViewModel"/> drives the queue panel and the status strip — its
/// <see cref="JobQueueViewModel.StatusText"/> and taskbar-progress properties are read
/// live off a real <see cref="JobQueueManager"/> rather than cached, so most of these
/// tests never need to pump the WPF dispatcher: reading a property after the queue's
/// own thread-safe state has settled is enough. The one thing that genuinely races
/// (JobAdded) is raised synchronously from <see cref="JobQueueManager.Enqueue"/> on the
/// calling thread, so <see cref="JobQueueViewModel.Rows"/> is safe to read immediately
/// after Enqueue too — nothing here relies on a job actually finishing on the
/// thread-pool thread that runs it, except where a test explicitly polls for that.
/// </summary>
public sealed class JobQueueViewModelTests : IDisposable
{
    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);

    private readonly string _workDir = Path.Combine(Path.GetTempPath(), "MediaSuite.App.Tests." + Guid.NewGuid().ToString("N"));
    private readonly Dispatcher _dispatcher = Dispatcher.CurrentDispatcher;

    public void Dispose()
    {
        if (Directory.Exists(_workDir))
        {
            Directory.Delete(_workDir, recursive: true);
        }
    }

    private JobQueueManager CreateQueue(FakeEngine engine, int maxConcurrency = 4) =>
        new(new EngineRegistry().Register(engine), new DiskTempWorkspaceFactory(_workDir), maxConcurrency);

    private static JobSpec Spec() => new()
    {
        OperationId = "video.convert",
        InputPaths = new[] { "clip.mp4" },
        Output = new OutputTarget { Directory = @"C:\out" },
    };

    /// <summary>
    /// Waits for <paramref name="job"/> to actually reach <see cref="JobStatus.Running"/> —
    /// deliberately not a poll of <see cref="JobQueueManager.RunningCount"/>: that count is
    /// booked synchronously inside <see cref="JobQueueManager.Enqueue"/>, before the
    /// thread-pool task that calls <c>MarkRunning</c> has necessarily run, so a caller that
    /// needs the row's own <c>Status</c>/<c>PercentComplete</c> to reflect "running" (as
    /// opposed to just "the queue has claimed a slot for it") needs to wait for the event
    /// the queue raises right after that call, not the slot count.
    /// </summary>
    private static async Task WaitForRunning(JobQueueManager queue, QueuedJob job)
    {
        var tcs = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        void OnStatusChanged(object? sender, QueuedJob changed)
        {
            if (ReferenceEquals(changed, job) && changed.Status == JobStatus.Running)
            {
                tcs.TrySetResult();
            }
        }

        queue.JobStatusChanged += OnStatusChanged;
        try
        {
            if (job.Status == JobStatus.Running)
            {
                tcs.TrySetResult();
            }

            await tcs.Task.WaitAsync(Timeout);
        }
        finally
        {
            queue.JobStatusChanged -= OnStatusChanged;
        }
    }

    [Fact]
    public void An_empty_queue_reads_as_idle_with_nothing_finished()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        Assert.False(viewModel.HasJobs);
        Assert.Equal("Queue idle — up to 4 jobs at once.", viewModel.StatusText);
        Assert.Equal(TaskbarItemProgressState.None, viewModel.TaskbarProgressState);
        Assert.False(viewModel.PauseResumeCommand.CanExecute(null));
        Assert.False(viewModel.CancelAllCommand.CanExecute(null));
        Assert.False(viewModel.ClearFinishedCommand.CanExecute(null));
    }

    [Fact]
    public void Enqueueing_a_job_while_paused_adds_a_row_without_starting_it()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        queue.Pause();
        var job = queue.Enqueue(Spec());

        Assert.True(viewModel.HasJobs);
        Assert.Same(job, Assert.Single(viewModel.Rows).Job);
        Assert.Equal(0, queue.RunningCount);
        Assert.Equal(1, queue.PendingCount);
        Assert.Equal("Paused — 0 of 4 slots busy, 1 waiting.", viewModel.StatusText);
        Assert.Equal(TaskbarItemProgressState.Paused, viewModel.TaskbarProgressState);
    }

    [Fact]
    public void Pausing_and_resuming_updates_the_label_and_the_status_text()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);
        queue.Enqueue(Spec());

        Assert.False(viewModel.IsPaused);
        viewModel.PauseResumeCommand.Execute(null);

        Assert.True(viewModel.IsPaused);
        Assert.Equal("Resume", viewModel.PauseResumeLabel);

        viewModel.PauseResumeCommand.Execute(null);

        Assert.False(viewModel.IsPaused);
        Assert.Equal("Pause", viewModel.PauseResumeLabel);
    }

    [Fact]
    public void Canceling_a_pending_job_through_CancelJobCommand_finishes_it_as_canceled()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        queue.Pause();
        queue.Enqueue(Spec());
        var row = Assert.Single(viewModel.Rows);

        viewModel.CancelJobCommand.Execute(row);

        // Canceling a still-pending job finishes it synchronously on the calling thread
        // (JobQueueManager.Cancel), so there is nothing to wait for here.
        Assert.Equal(JobStatus.Canceled, row.Job.Status);
        Assert.True(row.Job.IsFinished);
        Assert.True(viewModel.ClearFinishedCommand.CanExecute(null));
        Assert.Equal("Queue idle — 0 finished, 1 canceled.", viewModel.StatusText);
    }

    [Fact]
    public void CancelAllCommand_finishes_every_pending_job_as_canceled()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        queue.Pause();
        queue.Enqueue(Spec());
        queue.Enqueue(Spec());
        queue.Enqueue(Spec());

        Assert.True(viewModel.CancelAllCommand.CanExecute(null));
        viewModel.CancelAllCommand.Execute(null);

        Assert.All(viewModel.Rows, row => Assert.Equal(JobStatus.Canceled, row.Job.Status));
        Assert.Equal("Queue idle — 0 finished, 3 canceled.", viewModel.StatusText);
    }

    [Fact]
    public void ClearFinishedCommand_removes_only_finished_rows_and_leaves_the_rest()
    {
        using var queue = CreateQueue(FakeEngine.Instant());
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        queue.Pause();
        var canceled = queue.Enqueue(Spec());
        var stillPending = queue.Enqueue(Spec());
        queue.Cancel(canceled);

        Assert.True(viewModel.ClearFinishedCommand.CanExecute(null));

        viewModel.ClearFinishedCommand.Execute(null);

        var remaining = Assert.Single(viewModel.Rows);
        Assert.Same(stillPending, remaining.Job);
        Assert.False(viewModel.ClearFinishedCommand.CanExecute(null));
    }

    [Fact]
    public async Task A_running_job_shows_up_in_the_status_strip_and_the_taskbar_state()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(FakeEngine.Gated(gate));
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        var job = queue.Enqueue(Spec());
        await WaitForRunning(queue, job);

        Assert.Equal("Running — 1 of 4 slots busy.", viewModel.StatusText);
        // No progress reported yet, so the running row reads as indeterminate.
        Assert.Equal(TaskbarItemProgressState.Indeterminate, viewModel.TaskbarProgressState);

        gate.SetResult();
        await queue.WaitForIdleAsync().WaitAsync(Timeout);

        Assert.Equal(TaskbarItemProgressState.None, viewModel.TaskbarProgressState);
        Assert.Equal("Queue idle — 1 finished.", viewModel.StatusText);
    }

    [Fact]
    public async Task Progress_from_a_running_job_drives_the_taskbar_progress_value()
    {
        var gate = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        using var queue = CreateQueue(FakeEngine.Gated(gate));
        using var viewModel = new JobQueueViewModel(queue, _dispatcher);

        var job = queue.Enqueue(Spec());
        await WaitForRunning(queue, job);

        // Nothing else touches the job while the engine sits parked on the gate, so
        // reporting progress from the test thread here does not race the queue.
        job.ReportProgress(JobProgress.At(60, "Encoding"));

        Assert.Equal(TaskbarItemProgressState.Normal, viewModel.TaskbarProgressState);
        Assert.Equal(0.6, viewModel.TaskbarProgressValue, precision: 5);

        gate.SetResult();
        await queue.WaitForIdleAsync().WaitAsync(Timeout);
    }
}
