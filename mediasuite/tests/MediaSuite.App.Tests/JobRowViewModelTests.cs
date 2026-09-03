using System.Windows.Threading;
using MediaSuite.App.ViewModels;
using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.App.Tests;

/// <summary>
/// <see cref="JobRowViewModel"/> is what the queue panel actually binds to — its
/// computed properties (<see cref="JobRowViewModel.StatusText"/>,
/// <see cref="JobRowViewModel.HasDiagnostics"/> and the rest) are where a real user
/// would notice a regression first, long before anyone reading the underlying
/// <see cref="QueuedJob"/> or <see cref="JobResult"/> code would. Drives a real
/// <see cref="QueuedJob"/> through its internal lifecycle methods (this project has
/// <c>InternalsVisibleTo</c> from Core for exactly this) rather than a fake standing in
/// for it, so a change to <see cref="QueuedJob"/>'s own property-changed wiring would
/// show up here too.
/// </summary>
public class JobRowViewModelTests
{
    private static (QueuedJob Job, JobRowViewModel Row) CreateRow(string operationId = "video.convert", string inputPath = "clip.mp4")
    {
        var spec = new JobSpec
        {
            OperationId = operationId,
            InputPaths = new[] { inputPath },
            Output = new OutputTarget { Directory = @"C:\out" },
        };

        // The internal constructor is reachable here via InternalsVisibleTo (see
        // MediaSuite.Core.csproj) -- deliberately internal so nothing outside
        // JobQueueManager can fake a job's lifecycle in production.
        var job = new QueuedJob(spec, DateTimeOffset.UtcNow);
        var row = new JobRowViewModel(job, Dispatcher.CurrentDispatcher);
        return (job, row);
    }

    [Fact]
    public void A_pending_job_reads_as_Waiting()
    {
        var (_, row) = CreateRow();

        Assert.Equal("Waiting", row.StatusText);
        Assert.True(row.CanCancel);
        Assert.False(row.HasError);
    }

    [Fact]
    public void A_running_job_with_stage_and_percent_shows_both()
    {
        var (job, row) = CreateRow();

        job.MarkRunning(DateTimeOffset.UtcNow);
        job.ReportProgress(JobProgress.At(42, "Encoding"));

        Assert.Equal("Encoding — 42%", row.StatusText);
        Assert.Equal(42, row.Percent);
        Assert.False(row.IsIndeterminate);
    }

    [Fact]
    public void A_running_job_with_no_percentage_yet_is_indeterminate()
    {
        var (job, row) = CreateRow();

        job.MarkRunning(DateTimeOffset.UtcNow);
        job.ReportProgress(JobProgress.Indeterminate("Starting"));

        Assert.True(row.IsIndeterminate);
        Assert.Equal("Starting", row.StatusText);
        Assert.Equal(0, row.Percent);
    }

    [Fact]
    public void A_completed_job_reads_as_Done()
    {
        var (job, row) = CreateRow();

        job.MarkRunning(DateTimeOffset.UtcNow);
        job.Finish(JobResult.Success(new[] { @"C:\out\clip.mp4" }, TimeSpan.FromSeconds(1)), DateTimeOffset.UtcNow);

        Assert.Equal("Done", row.StatusText);
        Assert.False(row.CanCancel);
        Assert.False(row.HasError);
    }

    [Fact]
    public void A_completed_job_with_a_failed_upload_says_so()
    {
        var (job, row) = CreateRow();

        var result = JobResult.Success(new[] { @"C:\out\clip.mp4" }, TimeSpan.FromSeconds(1))
            with
        { UploadWarning = "Drive upload failed: offline" };
        job.Finish(result, DateTimeOffset.UtcNow);

        Assert.Equal("Done — Drive upload failed", row.StatusText);
        Assert.True(row.HasUploadWarning);
    }

    [Fact]
    public void A_failed_job_shows_its_message_and_no_details_button_without_diagnostics()
    {
        var (job, row) = CreateRow();

        job.Finish(JobResult.Failure("The tool is not installed."), DateTimeOffset.UtcNow);

        Assert.Equal("The tool is not installed.", row.StatusText);
        Assert.True(row.HasError);
        Assert.False(row.HasDiagnostics);
    }

    [Fact]
    public void A_failed_job_with_real_tool_output_offers_Copy_details()
    {
        var (job, row) = CreateRow();

        job.Finish(
            JobResult.Failure("FFmpeg could not convert 'clip.mp4': Invalid data found.",
                diagnostics: "Exit code: 1\n\n--- stderr ---\nInvalid data found when processing input"),
            DateTimeOffset.UtcNow);

        Assert.True(row.HasDiagnostics);
        Assert.Contains("Invalid data found when processing input", row.Diagnostics);
    }

    [Fact]
    public void A_canceled_job_reads_as_Canceled_and_never_offers_details()
    {
        var (job, row) = CreateRow();

        job.Finish(JobResult.Canceled(), DateTimeOffset.UtcNow);

        Assert.Equal("Canceled", row.StatusText);
        Assert.False(row.HasError);
        Assert.False(row.HasDiagnostics);
    }

    [Fact]
    public void Row_properties_update_live_as_the_underlying_job_changes()
    {
        var (job, row) = CreateRow();

        Assert.Equal("Waiting", row.StatusText);

        job.MarkRunning(DateTimeOffset.UtcNow);

        // QueuedJob raises PropertyChanged synchronously and JobRowViewModel's handler
        // checks Dispatcher.CheckAccess() before deciding whether to marshal -- on the
        // same thread that created the Dispatcher (this test thread), that check is true
        // and the update applies immediately, with nothing async to await here.
        Assert.Equal("Running", row.StatusText);
    }
}
