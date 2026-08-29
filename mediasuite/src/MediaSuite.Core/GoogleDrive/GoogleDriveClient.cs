using Google.Apis.Auth.OAuth2;
using Google.Apis.Auth.OAuth2.Responses;
using Google.Apis.Drive.v3;
using Google.Apis.Services;
using Google.Apis.Upload;
using Google.Apis.Util.Store;
using MediaSuite.Core.Settings;
using DriveFile = Google.Apis.Drive.v3.Data.File;

namespace MediaSuite.Core.GoogleDrive;

/// <summary>
/// The real Drive client, backed by Google's own .NET client library. Uses the
/// <c>drive.file</c> scope rather than full Drive access — it can only see files this app
/// itself created, never anything else already in the user's Drive, which is the
/// least-privilege choice for a personal, single-user app that only ever uploads.
/// </summary>
public sealed class GoogleDriveClient : IGoogleDriveClient, IDisposable
{
    private const string ApplicationName = "MediaSuite";
    private const string FolderMimeType = "application/vnd.google-apps.folder";
    private const string TokenUserId = "user";

    private static readonly string[] Scopes = { DriveService.Scope.DriveFile };

    private readonly AppSettings _settings;
    private readonly string _tokenDirectory;

    private DriveService? _service;

    public GoogleDriveClient(AppSettings settings, string? tokenDirectory = null)
    {
        _settings = settings ?? throw new ArgumentNullException(nameof(settings));
        _tokenDirectory = tokenDirectory ?? AppPaths.GoogleDriveTokenDirectory;
    }

    public async Task<bool> IsSignedInAsync(CancellationToken cancellationToken)
    {
        var store = new FileDataStore(_tokenDirectory, fullPath: true);
        var token = await store.GetAsync<TokenResponse>(TokenUserId).ConfigureAwait(false);
        return token is not null;
    }

    public async Task SignInAsync(CancellationToken cancellationToken)
    {
        var credentialsPath = _settings.ResolveGoogleDriveCredentialsPath();
        if (!File.Exists(credentialsPath))
        {
            throw new FileNotFoundException(
                "No Google Drive OAuth client file found. Settings → Google Drive explains how to create one.",
                credentialsPath);
        }

        GoogleClientSecrets secrets;
        await using (var stream = File.OpenRead(credentialsPath))
        {
            secrets = GoogleClientSecrets.FromStream(stream);
        }

        var credential = await GoogleWebAuthorizationBroker.AuthorizeAsync(
            secrets.Secrets,
            Scopes,
            TokenUserId,
            cancellationToken,
            new FileDataStore(_tokenDirectory, fullPath: true)).ConfigureAwait(false);

        _service?.Dispose();
        _service = new DriveService(new BaseClientService.Initializer
        {
            HttpClientInitializer = credential,
            ApplicationName = ApplicationName,
        });
    }

    public Task SignOutAsync()
    {
        _service?.Dispose();
        _service = null;

        if (Directory.Exists(_tokenDirectory))
        {
            Directory.Delete(_tokenDirectory, recursive: true);
        }

        return Task.CompletedTask;
    }

    public async Task<IReadOnlyList<GoogleDriveFolder>> ListFoldersAsync(
        string? parentFolderId, CancellationToken cancellationToken)
    {
        var service = await RequireServiceAsync(cancellationToken).ConfigureAwait(false);

        var parent = string.IsNullOrWhiteSpace(parentFolderId) ? "root" : parentFolderId;
        var request = service.Files.List();
        request.Q = $"'{EscapeForQuery(parent)}' in parents and mimeType = '{FolderMimeType}' and trashed = false";
        request.Fields = "files(id, name)";
        request.Spaces = "drive";

        var response = await request.ExecuteAsync(cancellationToken).ConfigureAwait(false);

        return response.Files
            .Select(file => new GoogleDriveFolder { Id = file.Id, Name = file.Name })
            .OrderBy(folder => folder.Name, StringComparer.OrdinalIgnoreCase)
            .ToList();
    }

    public async Task<string> CreateFolderAsync(
        string name, string? parentFolderId, CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(name);

        var service = await RequireServiceAsync(cancellationToken).ConfigureAwait(false);

        var metadata = new DriveFile
        {
            Name = name,
            MimeType = FolderMimeType,
            Parents = string.IsNullOrWhiteSpace(parentFolderId) ? null : new List<string> { parentFolderId },
        };

        var request = service.Files.Create(metadata);
        request.Fields = "id";

        var created = await request.ExecuteAsync(cancellationToken).ConfigureAwait(false);
        return created.Id;
    }

    public async Task<GoogleDriveUploadResult> UploadFileAsync(
        string localPath,
        string? folderId,
        IProgress<double>? progress,
        CancellationToken cancellationToken)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(localPath);

        var service = await RequireServiceAsync(cancellationToken).ConfigureAwait(false);
        var fileName = Path.GetFileName(localPath);

        var metadata = new DriveFile
        {
            Name = fileName,
            Parents = string.IsNullOrWhiteSpace(folderId) ? null : new List<string> { folderId },
        };

        progress?.Report(0);

        await using var content = File.OpenRead(localPath);
        var request = service.Files.Create(metadata, content, MimeTypeFor(fileName));
        request.Fields = "id, name";

        var upload = await request.UploadAsync(cancellationToken).ConfigureAwait(false);
        if (upload.Status != UploadStatus.Completed)
        {
            throw upload.Exception ?? new IOException($"Upload of '{fileName}' to Google Drive did not complete.");
        }

        progress?.Report(100);

        var uploaded = request.ResponseBody;
        return new GoogleDriveUploadResult { FileId = uploaded.Id, FileName = uploaded.Name };
    }

    public void Dispose()
    {
        _service?.Dispose();
        _service = null;
    }

    /// <summary>
    /// Rebuilds the service from a cached token when one exists, without ever popping the
    /// interactive consent screen — the queue runs jobs unattended, so an operation
    /// attempted before the app has ever been signed in must fail clearly instead.
    /// </summary>
    private async Task<DriveService> RequireServiceAsync(CancellationToken cancellationToken)
    {
        if (_service is not null)
        {
            return _service;
        }

        if (!await IsSignedInAsync(cancellationToken).ConfigureAwait(false))
        {
            throw new GoogleDriveNotSignedInException();
        }

        await SignInAsync(cancellationToken).ConfigureAwait(false);
        return _service!;
    }

    private static string EscapeForQuery(string value) => value.Replace("'", "\\'");

    /// <summary>
    /// Best-effort content type for the handful of formats this app actually produces.
    /// Drive stores and serves the file correctly either way; this only affects how the
    /// Drive web UI previews it, so an unrecognised extension falling back to a generic
    /// type is not a real loss.
    /// </summary>
    private static string MimeTypeFor(string fileName)
    {
        var extension = Path.GetExtension(fileName).TrimStart('.').ToLowerInvariant();

        return extension switch
        {
            "pdf" => "application/pdf",
            "png" => "image/png",
            "jpg" or "jpeg" => "image/jpeg",
            "webp" => "image/webp",
            "gif" => "image/gif",
            "svg" => "image/svg+xml",
            "mp4" => "video/mp4",
            "webm" => "video/webm",
            "mp3" => "audio/mpeg",
            "wav" => "audio/wav",
            "zip" => "application/zip",
            "7z" => "application/x-7z-compressed",
            "epub" => "application/epub+zip",
            "docx" => "application/vnd.openxmlformats-officedocument.wordprocessingml.document",
            "txt" => "text/plain",
            _ => "application/octet-stream",
        };
    }
}
