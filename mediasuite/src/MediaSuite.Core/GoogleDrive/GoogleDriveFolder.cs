namespace MediaSuite.Core.GoogleDrive;

/// <summary>A Drive folder as the picker sees it.</summary>
public sealed record GoogleDriveFolder
{
    public required string Id { get; init; }

    public required string Name { get; init; }
}
