using System.Collections.ObjectModel;
using System.Windows.Input;
using System.Windows.Threading;
using MediaSuite.App.Mvvm;
using MediaSuite.Core.Jobs;

namespace MediaSuite.App.ViewModels;

/// <summary>
/// The queue panel and status strip: live rows, counts, pause/resume and cancel.
/// </summary>
public sealed class JobQueueViewModel : ObservableObject, IDisposable
{
    private readonly JobQueueManager _queue;
    private readonly Dispatcher _dispatcher;
    private bool _disposed;

    public JobQueueViewModel(JobQueueManager queue, Dispatcher dispatcher)
    {
        _queue = queue;
        _dispatcher = dispatcher;

        Rows = new ObservableCollection<JobRowViewModel>();

        PauseResumeCommand = new RelayCommand(TogglePause, () => Rows.Count > 0);
        CancelAllCommand = new RelayCommand(_queue.CancelAll, () => _queue.RunningCount + _queue.PendingCount > 0);
        ClearFinishedCommand = new RelayCommand(ClearFinished, () => Rows.Any(row => row.Job.IsFinished));
        CancelJobCommand = new RelayCommand(parameter =>
        {
            if (parameter is JobRowViewModel row)
            {
                _queue.Cancel(row.Job);
            }
        });

        _queue.JobAdded += OnJobAdded;
        _queue.JobStatusChanged += OnQueueChanged;
        _queue.JobProgressChanged += OnQueueChanged;
        _queue.QueueIdle += OnQueueIdle;
    }

    public ObservableCollection<JobRowViewModel> Rows { get; }

    public ICommand PauseResumeCommand { get; }

    public ICommand CancelAllCommand { get; }

    public ICommand ClearFinishedCommand { get; }

    public ICommand CancelJobCommand { get; }

    public bool HasJobs => Rows.Count > 0;

    public bool IsPaused => _queue.IsPaused;

    public string PauseResumeLabel => IsPaused ? "Resume" : "Pause";

    /// <summary>One line for the status strip.</summary>
    public string StatusText
    {
        get
        {
            var running = _queue.RunningCount;
            var pending = _queue.PendingCount;

            if (running == 0 && pending == 0)
            {
                var finished = Rows.Count(row => row.Job.IsFinished);
                if (finished == 0)
                {
                    return $"Queue idle — up to {_queue.MaxConcurrency} jobs at once.";
                }

                var failed = Rows.Count(row => row.Job.Status == JobStatus.Failed);
                var canceled = Rows.Count(row => row.Job.Status == JobStatus.Canceled);
                var done = finished - failed - canceled;

                var parts = new List<string> { $"{done} finished" };
                if (failed > 0)
                {
                    parts.Add($"{failed} failed");
                }

                if (canceled > 0)
                {
                    parts.Add($"{canceled} canceled");
                }

                return $"Queue idle — {string.Join(", ", parts)}.";
            }

            var state = _queue.IsPaused ? "Paused" : "Running";
            return pending > 0
                ? $"{state} — {running} of {_queue.MaxConcurrency} slots busy, {pending} waiting."
                : $"{state} — {running} of {_queue.MaxConcurrency} slots busy.";
        }
    }

    /// <summary>Applies the Settings concurrency slider to the live queue.</summary>
    public void SetMaxConcurrency(int value)
    {
        _queue.MaxConcurrency = value;
        RefreshCounts();
    }

    private void TogglePause()
    {
        if (_queue.IsPaused)
        {
            _queue.Resume();
        }
        else
        {
            _queue.Pause();
        }

        OnPropertyChanged(nameof(IsPaused));
        OnPropertyChanged(nameof(PauseResumeLabel));
        RefreshCounts();
    }

    private void ClearFinished()
    {
        foreach (var row in Rows.Where(row => row.Job.IsFinished).ToList())
        {
            row.Dispose();
            Rows.Remove(row);
        }

        RefreshCounts();
    }

    private void OnJobAdded(object? sender, QueuedJob job) =>
        OnDispatcher(() =>
        {
            Rows.Add(new JobRowViewModel(job, _dispatcher));
            OnPropertyChanged(nameof(HasJobs));
            RefreshCounts();
        });

    private void OnQueueIdle(object? sender, EventArgs e) => OnDispatcher(RefreshCounts);

    private void OnQueueChanged(object? sender, QueuedJob job) => OnDispatcher(RefreshCounts);

    private void RefreshCounts()
    {
        OnPropertyChanged(nameof(StatusText));
        OnPropertyChanged(nameof(HasJobs));

        // RelayCommand hangs its CanExecuteChanged off RequerySuggested, which only fires
        // on user input — a job finishing on its own would otherwise leave the buttons stale.
        CommandManager.InvalidateRequerySuggested();
    }

    private void OnDispatcher(Action action)
    {
        if (_disposed)
        {
            return;
        }

        if (_dispatcher.CheckAccess())
        {
            action();
        }
        else
        {
            _ = _dispatcher.InvokeAsync(action);
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;

        _queue.JobAdded -= OnJobAdded;
        _queue.JobStatusChanged -= OnQueueChanged;
        _queue.JobProgressChanged -= OnQueueChanged;
        _queue.QueueIdle -= OnQueueIdle;

        foreach (var row in Rows)
        {
            row.Dispose();
        }
    }
}
