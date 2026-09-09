using System.ComponentModel;
using System.Runtime.CompilerServices;

namespace MediaSuite.Core.Jobs;

/// <summary>
/// One job as the queue sees it: the spec plus its live status and progress.
/// </summary>
/// <remarks>
/// Raises <see cref="PropertyChanged"/> from whichever thread the engine reports on —
/// callers that bind to it are responsible for marshalling.
/// </remarks>
public sealed class QueuedJob : INotifyPropertyChanged
{
    private JobStatus _status = JobStatus.Pending;
    private double? _percentComplete;
    private string? _stage;
    private string? _currentItem;
    private JobResult? _result;
    private DateTimeOffset? _startedAt;
    private DateTimeOffset? _finishedAt;
    private int _completionClaimed;

    internal QueuedJob(JobSpec spec, DateTimeOffset enqueuedAt)
    {
        Spec = spec;
        EnqueuedAt = enqueuedAt;
    }

    public event PropertyChangedEventHandler? PropertyChanged;

    public Guid Id { get; } = Guid.NewGuid();

    public JobSpec Spec { get; }

    public string DisplayName => Spec.DisplayName;

    public DateTimeOffset EnqueuedAt { get; }

    public JobStatus Status
    {
        get => _status;
        private set
        {
            if (Set(ref _status, value))
            {
                OnPropertyChanged(nameof(IsFinished));
                OnPropertyChanged(nameof(IsActive));
            }
        }
    }

    /// <summary>0-100, or null while the engine cannot estimate.</summary>
    public double? PercentComplete
    {
        get => _percentComplete;
        private set => Set(ref _percentComplete, value);
    }

    /// <summary>What the engine is doing right now ("Encoding", "Upscaling").</summary>
    public string? Stage
    {
        get => _stage;
        private set => Set(ref _stage, value);
    }

    /// <summary>File being processed, for batch jobs.</summary>
    public string? CurrentItem
    {
        get => _currentItem;
        private set => Set(ref _currentItem, value);
    }

    public JobResult? Result
    {
        get => _result;
        private set => Set(ref _result, value);
    }

    public DateTimeOffset? StartedAt
    {
        get => _startedAt;
        private set => Set(ref _startedAt, value);
    }

    public DateTimeOffset? FinishedAt
    {
        get => _finishedAt;
        private set => Set(ref _finishedAt, value);
    }

    public bool IsFinished => Status is JobStatus.Completed or JobStatus.Failed or JobStatus.Canceled;

    public bool IsActive => Status is JobStatus.Pending or JobStatus.Running;

    /// <summary>Failure message, or null while the job has not failed.</summary>
    public string? ErrorMessage => Result?.ErrorMessage;

    internal void MarkRunning(DateTimeOffset startedAt)
    {
        StartedAt = startedAt;
        Status = JobStatus.Running;
    }

    internal void ReportProgress(JobProgress progress)
    {
        PercentComplete = progress.Percent;

        if (progress.Stage is not null)
        {
            Stage = progress.Stage;
        }

        if (progress.CurrentItem is not null)
        {
            CurrentItem = progress.CurrentItem;
        }
    }

    /// <summary>
    /// Claims the right to record this job's outcome. Returns true exactly once, so a
    /// cancel racing an engine's own completion cannot post two results.
    /// </summary>
    internal bool TryClaimCompletion() => Interlocked.Exchange(ref _completionClaimed, 1) == 0;

    /// <summary>
    /// True once something has claimed the right to record this job's outcome (see
    /// <see cref="TryClaimCompletion"/>), even if <see cref="Status"/> itself hasn't been
    /// updated yet. <see cref="JobQueueManager"/>'s own dequeue loop checks this, under the
    /// same lock a pending cancellation claims completion under, so it can never start a
    /// job whose cancellation has already been claimed but not yet fully recorded --
    /// checking <see cref="IsFinished"/> alone is not enough, since that only becomes true
    /// once <c>Finish</c> actually runs, which for a cancelled-while-pending job happens
    /// just after the claim, not atomically with it.
    /// </summary>
    internal bool CompletionClaimed => Interlocked.CompareExchange(ref _completionClaimed, 0, 0) == 1;

    internal void Finish(JobResult result, DateTimeOffset finishedAt)
    {
        Result = result;
        FinishedAt = finishedAt;
        Status = result.Status;

        if (result.IsSuccess)
        {
            PercentComplete = 100;
        }

        OnPropertyChanged(nameof(ErrorMessage));
    }

    private bool Set<T>(ref T field, T value, [CallerMemberName] string? propertyName = null)
    {
        if (EqualityComparer<T>.Default.Equals(field, value))
        {
            return false;
        }

        field = value;
        OnPropertyChanged(propertyName);
        return true;
    }

    private void OnPropertyChanged(string? propertyName) =>
        PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
}
