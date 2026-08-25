namespace MediaSuite.Core.Jobs;

/// <summary>
/// Where the results of a job go. A job can write locally, upload to Drive, or both;
/// Drive upload is off unless <see cref="UploadToGoogleDrive"/> is set explicitly.
/// </summary>
public sealed record OutputTarget
{
    /// <summary>Folder that receives the output files.</summary>
    public required string Directory { get; init; }

    /// <summary>Target extension without a leading dot (e.g. "mp4"). Null for tools that keep the input format.</summary>
    public string? Format { get; init; }

    /// <summary>
    /// Name template for the produced file. Supported tokens: <c>{name}</c> (input file
    /// name without extension), <c>{ext}</c> (target extension), <c>{index}</c>
    /// (1-based position within the batch).
    /// </summary>
    public string FileNameTemplate { get; init; } = "{name}.{ext}";

    /// <summary>
    /// For batches taken from nested folders: recreate the source folder tree under
    /// <see cref="Directory"/> instead of flattening everything into one folder.
    /// </summary>
    public bool PreserveFolderStructure { get; init; }

    /// <summary>What to do when the destination file already exists.</summary>
    public OverwritePolicy OverwritePolicy { get; init; } = OverwritePolicy.Rename;

    /// <summary>Per-job Google Drive upload toggle. Off by default, as agreed.</summary>
    public bool UploadToGoogleDrive { get; init; }

    /// <summary>Drive folder id chosen (or created) at upload time. Null means Drive root.</summary>
    public string? GoogleDriveFolderId { get; init; }
}
