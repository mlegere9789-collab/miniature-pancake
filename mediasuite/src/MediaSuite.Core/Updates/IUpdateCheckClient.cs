namespace MediaSuite.Core.Updates;

/// <summary>
/// The seam between the app and wherever it checks for a newer release — the same role
/// <c>IGoogleDriveClient</c> plays for Drive, so the app never depends on the real
/// network call to decide what to show.
/// </summary>
public interface IUpdateCheckClient
{
    /// <summary>Never throws for an ordinary failed check — see <see cref="UpdateCheckResult.ErrorMessage"/>.</summary>
    Task<UpdateCheckResult> CheckAsync(CancellationToken cancellationToken);
}
