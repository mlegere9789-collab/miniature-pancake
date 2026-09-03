using System.Threading;
using System.Threading.Tasks;
using MediaSuite.Core.GoogleDrive;

namespace MediaSuite.App.Tests;

/// <summary>
/// Stand-in Drive client for ModulePageViewModelTests. Every method returns an
/// already-completed (or already-faulted) task deliberately: <c>ModulePageViewModel</c>
/// fires its Drive calls with a bare <c>_ = ...Async()</c> (no way for a test to await
/// them), and awaiting a task that is already complete at await-time continues
/// synchronously on the calling thread rather than posting a continuation — so with these
/// fakes, a whole refresh runs to completion inside the property setter or command that
/// triggered it, with nothing left to race or pump a dispatcher for.
/// </summary>
public sealed class FakeGoogleDriveClient : IGoogleDriveClient
{
    private int _nextFolderId = 1;

    public IReadOnlyList<GoogleDriveFolder> FoldersToReturn { get; set; } = Array.Empty<GoogleDriveFolder>();

    public Exception? ListFoldersFailure { get; set; }

    public List<(string Name, string? ParentFolderId)> CreatedFolders { get; } = new();

    public Task<bool> IsSignedInAsync(CancellationToken cancellationToken) => Task.FromResult(true);

    public Task SignInAsync(CancellationToken cancellationToken) => Task.CompletedTask;

    public Task SignOutAsync() => Task.CompletedTask;

    public Task<IReadOnlyList<GoogleDriveFolder>> ListFoldersAsync(string? parentFolderId, CancellationToken cancellationToken) =>
        ListFoldersFailure is not null
            ? Task.FromException<IReadOnlyList<GoogleDriveFolder>>(ListFoldersFailure)
            : Task.FromResult(FoldersToReturn);

    public Task<string> CreateFolderAsync(string name, string? parentFolderId, CancellationToken cancellationToken)
    {
        CreatedFolders.Add((name, parentFolderId));
        return Task.FromResult((_nextFolderId++).ToString());
    }

    public Task<GoogleDriveUploadResult> UploadFileAsync(
        string localPath,
        string? folderId,
        IProgress<double>? progress,
        CancellationToken cancellationToken) =>
        throw new NotSupportedException("Not exercised by ModulePageViewModelTests.");
}
