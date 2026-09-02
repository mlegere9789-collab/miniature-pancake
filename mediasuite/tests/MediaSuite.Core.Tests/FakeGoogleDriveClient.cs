using MediaSuite.Core.GoogleDrive;

namespace MediaSuite.Core.Tests;

/// <summary>Test double standing in for a signed-in (or not) Drive account.</summary>
public sealed class FakeGoogleDriveClient : IGoogleDriveClient
{
    private readonly object _gate = new();
    private bool _signedIn;

    public FakeGoogleDriveClient(bool startSignedIn = true) => _signedIn = startSignedIn;

    /// <summary>Set to make every upload throw this instead of succeeding.</summary>
    public Exception? UploadFailure { get; set; }

    /// <summary>When set, UploadFileAsync blocks here until it completes or is canceled.</summary>
    public TaskCompletionSource? UploadGate { get; set; }

    /// <summary>Folders ListFoldersAsync and CreateFolderAsync operate on. Empty by default.</summary>
    public List<GoogleDriveFolder> Folders { get; } = new();

    /// <summary>Local paths passed to UploadFileAsync, in call order.</summary>
    public List<string> UploadedPaths { get; } = new();

    /// <summary>Folder ids passed alongside each upload, in call order.</summary>
    public List<string?> UploadedFolderIds { get; } = new();

    public Task<bool> IsSignedInAsync(CancellationToken cancellationToken) => Task.FromResult(_signedIn);

    public Task SignInAsync(CancellationToken cancellationToken)
    {
        _signedIn = true;
        return Task.CompletedTask;
    }

    public Task SignOutAsync()
    {
        _signedIn = false;
        return Task.CompletedTask;
    }

    public Task<IReadOnlyList<GoogleDriveFolder>> ListFoldersAsync(
        string? parentFolderId, CancellationToken cancellationToken)
    {
        RequireSignedIn();
        return Task.FromResult<IReadOnlyList<GoogleDriveFolder>>(Folders.ToList());
    }

    public Task<string> CreateFolderAsync(string name, string? parentFolderId, CancellationToken cancellationToken)
    {
        RequireSignedIn();

        var id = $"folder-{name}";
        Folders.Add(new GoogleDriveFolder { Id = id, Name = name });
        return Task.FromResult(id);
    }

    public async Task<GoogleDriveUploadResult> UploadFileAsync(
        string localPath, string? folderId, IProgress<double>? progress, CancellationToken cancellationToken)
    {
        RequireSignedIn();

        lock (_gate)
        {
            UploadedPaths.Add(localPath);
            UploadedFolderIds.Add(folderId);
        }

        if (UploadGate is not null)
        {
            await UploadGate.Task.WaitAsync(cancellationToken).ConfigureAwait(false);
        }

        if (UploadFailure is not null)
        {
            throw UploadFailure;
        }

        progress?.Report(100);

        var fileName = Path.GetFileName(localPath);
        return new GoogleDriveUploadResult { FileId = $"file-{fileName}", FileName = fileName };
    }

    private void RequireSignedIn()
    {
        if (!_signedIn)
        {
            throw new GoogleDriveNotSignedInException();
        }
    }
}
