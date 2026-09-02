using MediaSuite.Core.Jobs;
using Xunit;

namespace MediaSuite.Core.Tests;

public class JobQueueManagerGoogleDriveTests : IDisposable
{
    private readonly TempDirectory _temp = new();

    private static readonly TimeSpan Timeout = TimeSpan.FromSeconds(10);

    public void Dispose() => _temp.Dispose();

    private JobQueueManager CreateQueue(FakeGoogleDriveClient? driveClient, Func<IConversionEngine>? engineFactory = null)
    {
        var registry = new EngineRegistry().Register((engineFactory ?? (() => FakeEngine.Instant()))());
        return new JobQueueManager(
            registry,
            new DiskTempWorkspaceFactory(_temp.Combine("work")),
            maxConcurrency: 4,
            driveClient: driveClient);
    }

    private static JobSpec Spec(bool uploadToGoogleDrive, string? folderId = null) => new()
    {
        OperationId = "image.convert",
        InputPaths = new[] { "input.png" },
        Output = new OutputTarget
        {
            Directory = "out",
            Format = "jpg",
            UploadToGoogleDrive = uploadToGoogleDrive,
            GoogleDriveFolderId = folderId,
        },
    };

    private static async Task WaitForIdle(JobQueueManager queue) =>
        await queue.WaitForIdleAsync().WaitAsync(Timeout);

    [Fact]
    public async Task A_job_that_does_not_ask_for_upload_never_touches_drive()
    {
        var drive = new FakeGoogleDriveClient();
        using var queue = CreateQueue(drive);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: false));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.Empty(drive.UploadedPaths);
        Assert.Null(job.Result!.UploadWarning);
    }

    [Fact]
    public async Task A_successful_upload_leaves_no_warning_and_uploads_every_output()
    {
        var drive = new FakeGoogleDriveClient();
        var engine = FakeEngineProducing("photo.jpg", "thumb.jpg");
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true, folderId: "folder-123"));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.Null(job.Result!.UploadWarning);
        Assert.Equal(new[] { "photo.jpg", "thumb.jpg" }, drive.UploadedPaths);
        Assert.All(drive.UploadedFolderIds, id => Assert.Equal("folder-123", id));
    }

    [Fact]
    public async Task A_failed_upload_still_completes_the_job_with_a_warning()
    {
        var drive = new FakeGoogleDriveClient { UploadFailure = new IOException("network reset") };
        var engine = FakeEngineProducing("photo.jpg");
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.NotNull(job.Result!.UploadWarning);
        Assert.Contains("network reset", job.Result.UploadWarning, StringComparison.Ordinal);
        Assert.Single(job.Result.OutputPaths);
    }

    [Fact]
    public async Task Uploading_while_never_signed_in_completes_the_job_with_a_clear_warning()
    {
        var drive = new FakeGoogleDriveClient(startSignedIn: false);
        var engine = FakeEngineProducing("photo.jpg");
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.Contains("sign in", job.Result!.UploadWarning!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Uploading_with_no_drive_client_configured_completes_the_job_with_a_warning()
    {
        var engine = FakeEngineProducing("photo.jpg");
        using var queue = CreateQueue(driveClient: null, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Completed, job.Status);
        Assert.Contains("not set up", job.Result!.UploadWarning!, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task A_conversion_that_fails_never_attempts_an_upload()
    {
        var drive = new FakeGoogleDriveClient();
        var engine = new FakeEngine((_, _, _) => Task.FromResult(JobResult.Failure("bad input")));
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));
        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Failed, job.Status);
        Assert.Empty(drive.UploadedPaths);
    }

    [Fact]
    public async Task Cancelling_a_job_mid_upload_ends_it_canceled_not_completed()
    {
        var drive = new FakeGoogleDriveClient { UploadGate = new TaskCompletionSource() };
        var engine = FakeEngineProducing("photo.jpg");
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));

        await WaitUntil(() => drive.UploadedPaths.Count == 1);
        queue.Cancel(job);

        await WaitForIdle(queue);

        Assert.Equal(JobStatus.Canceled, job.Status);
    }

    [Fact]
    public async Task Uploading_reports_an_Uploading_progress_stage()
    {
        var drive = new FakeGoogleDriveClient();
        var engine = FakeEngineProducing("photo.jpg");
        using var queue = CreateQueue(drive, () => engine);

        var job = queue.Enqueue(Spec(uploadToGoogleDrive: true));
        await WaitForIdle(queue);

        Assert.Equal("Uploading", job.Stage);
    }

    private static FakeEngine FakeEngineProducing(params string[] outputs) =>
        new((_, _, _) => Task.FromResult(JobResult.Success(outputs, TimeSpan.Zero)));

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

        Assert.Fail("Timed out waiting for the expected state.");
    }
}
