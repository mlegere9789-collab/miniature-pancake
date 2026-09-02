namespace MediaSuite.Core.GoogleDrive;

/// <summary>
/// The seam between the job queue and Google Drive — the same role <c>IProcessRunner</c>
/// plays for the bundled conversion tools, except this one talks to a Google account
/// instead of a local binary, so engines and the queue never depend on the real client.
/// </summary>
public interface IGoogleDriveClient
{
    /// <summary>
    /// True once a signed-in account's token is cached and usable. Never prompts —
    /// callers that need an interactive sign-in call <see cref="SignInAsync"/> instead.
    /// </summary>
    Task<bool> IsSignedInAsync(CancellationToken cancellationToken);

    /// <summary>Runs the OAuth consent flow if needed and caches the resulting token.</summary>
    Task SignInAsync(CancellationToken cancellationToken);

    /// <summary>Forgets the cached token locally. Does not revoke it on Google's side.</summary>
    Task SignOutAsync();

    /// <summary>
    /// Folders directly under <paramref name="parentFolderId"/>, or Drive root when null.
    /// Throws <see cref="GoogleDriveNotSignedInException"/> if nothing is signed in.
    /// </summary>
    Task<IReadOnlyList<GoogleDriveFolder>> ListFoldersAsync(string? parentFolderId, CancellationToken cancellationToken);

    /// <summary>
    /// Creates a folder and returns its id. Throws <see cref="GoogleDriveNotSignedInException"/>
    /// if nothing is signed in.
    /// </summary>
    Task<string> CreateFolderAsync(string name, string? parentFolderId, CancellationToken cancellationToken);

    /// <summary>
    /// Uploads one file, into <paramref name="folderId"/> or Drive root when null. Throws
    /// <see cref="GoogleDriveNotSignedInException"/> if nothing is signed in.
    /// </summary>
    Task<GoogleDriveUploadResult> UploadFileAsync(
        string localPath,
        string? folderId,
        IProgress<double>? progress,
        CancellationToken cancellationToken);
}
