namespace MediaSuite.Core.Tooling;

/// <summary>
/// Static facts about one bundled binary: what it is called on disk, what it is for,
/// and what licence it ships under.
/// </summary>
/// <param name="Id">Stable identifier.</param>
/// <param name="DisplayName">Name shown in Settings and in missing-dependency messages.</param>
/// <param name="FolderName">Sub-folder of <c>tools\</c> the binary is unpacked into.</param>
/// <param name="ExecutableNames">Candidate file names, most preferred first (Windows names).</param>
/// <param name="Purpose">One line explaining which features stop working without it.</param>
/// <param name="License">Licence of the shipped build.</param>
/// <param name="SourceUrl">Where the build is fetched from.</param>
/// <param name="IsRequired">
/// True when the app is largely useless without it; false for tools that only power
/// a subset of features (the UI disables just those).
/// </param>
public sealed record ToolDescriptor(
    ExternalToolId Id,
    string DisplayName,
    string FolderName,
    IReadOnlyList<string> ExecutableNames,
    string Purpose,
    string License,
    string SourceUrl,
    bool IsRequired = false);
