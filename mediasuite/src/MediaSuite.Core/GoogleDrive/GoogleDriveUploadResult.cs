namespace MediaSuite.Core.GoogleDrive;

/// <summary>Outcome of uploading one file to Drive.</summary>
public sealed record GoogleDriveUploadResult
{
    public required string FileId { get; init; }

    public required string FileName { get; init; }
}
