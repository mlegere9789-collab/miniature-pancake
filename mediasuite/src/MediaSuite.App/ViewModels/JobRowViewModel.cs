using System.ComponentModel;
using System.Windows.Threading;
using MediaSuite.App.Mvvm;
using MediaSuite.Core.Jobs;

namespace MediaSuite.App.ViewModels;

/// <summary>
/// One row in the queue panel.
/// </summary>
/// <remarks>
/// Engines report progress from whatever thread they run on, so every notification from
/// the underlying <see cref="QueuedJob"/> is bounced onto the dispatcher before the
/// bindings see it.
/// </remarks>
public sealed class JobRowViewModel : ObservableObject, IDisposable
{
    private readonly Dispatcher _dispatcher;
    private bool _disposed;

    public JobRowViewModel(QueuedJob job, Dispatcher dispatcher)
    {
        Job = job;
        _dispatcher = dispatcher;
        job.PropertyChanged += OnJobPropertyChanged;
    }

    public QueuedJob Job { get; }

    public string DisplayName => Job.DisplayName;

    public string OperationId => Job.Spec.OperationId;

    public double Percent => Job.PercentComplete ?? 0;

    /// <summary>True while the engine cannot say how far along it is.</summary>
    public bool IsIndeterminate => Job.Status == JobStatus.Running && Job.PercentComplete is null;

    public bool CanCancel => !Job.IsFinished;

    public string? ErrorMessage => Job.ErrorMessage;

    public bool HasError => Job.Status == JobStatus.Failed;

    /// <summary>
    /// Raw tool output kept for diagnostics — usually the tail of the underlying process's
    /// stderr, which is often the only place the *real* reason a job failed shows up
    /// (an FFmpeg option rejected by that build, a tool exiting non-zero with no other
    /// explanation). <see cref="ErrorMessage"/> is short by design; this is the detail a
    /// bug report or a support request actually needs.
    /// </summary>
    public string? Diagnostics => Job.Result?.Diagnostics;

    public bool HasDiagnostics => !string.IsNullOrWhiteSpace(Diagnostics);

    /// <summary>Set when the local output succeeded but the requested Drive upload did not.</summary>
    public string? UploadWarning => Job.Result?.UploadWarning;

    public bool HasUploadWarning => !string.IsNullOrEmpty(UploadWarning);

    public string StatusText => Job.Status switch
    {
        JobStatus.Pending => "Waiting",
        JobStatus.Running => Job.Stage is { Length: > 0 } stage
            ? (Job.PercentComplete is { } percent ? $"{stage} — {percent:0}%" : stage)
            : (Job.PercentComplete is { } onlyPercent ? $"{onlyPercent:0}%" : "Running"),
        JobStatus.Completed => HasUploadWarning ? "Done — Drive upload failed" : "Done",
        JobStatus.Failed => Job.ErrorMessage ?? "Failed",
        JobStatus.Canceled => "Canceled",
        JobStatus.Paused => "Paused",
        _ => Job.Status.ToString(),
    };

    private void OnJobPropertyChanged(object? sender, PropertyChangedEventArgs e)
    {
        if (_disposed)
        {
            return;
        }

        // Null property name means "everything changed", which is what we want: one job
        // notification can move the status text, the bar and the cancel button at once.
        if (_dispatcher.CheckAccess())
        {
            OnPropertyChanged(null);
        }
        else
        {
            _ = _dispatcher.InvokeAsync(() => OnPropertyChanged(null));
        }
    }

    public void Dispose()
    {
        if (_disposed)
        {
            return;
        }

        _disposed = true;
        Job.PropertyChanged -= OnJobPropertyChanged;
    }
}
