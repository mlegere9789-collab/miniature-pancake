namespace MediaSuite.Core.GoogleDrive;

/// <summary>
/// Thrown when a folder listing or upload is attempted before the app has ever been
/// signed in — the queue must not pop an interactive consent screen mid-job, so it fails
/// clearly instead and points at where to sign in.
/// </summary>
public sealed class GoogleDriveNotSignedInException : InvalidOperationException
{
    public GoogleDriveNotSignedInException()
        : base("Not signed in to Google Drive — sign in from Settings first.")
    {
    }
}
