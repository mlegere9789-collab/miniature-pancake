namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds 7-Zip command lines. Pure, so every argument choice can be checked without
/// 7-Zip installed.
/// </summary>
/// <remarks>
/// 7-Zip has no single "convert" command — there is no flag that reads one archive format
/// and writes another directly. Converting is genuinely two steps: extract the source
/// archive, then create a new archive of what came out. This mirrors how the PDF module
/// turns HEIC into a JPEG before MuPDF ever sees it, and how the document module has
/// LibreOffice write to a folder rather than a file — a real limitation of the tool, not
/// an implementation shortcut.
/// </remarks>
public static class ArchiveCommandBuilder
{
    /// <summary>7-Zip's own name for an archive type, given its file extension.</summary>
    public static string ArchiveTypeFor(string extension) => extension.TrimStart('.').ToLowerInvariant() switch
    {
        "zip" => "zip",
        "7z" => "7z",
        "tar" => "tar",
        "gz" or "gzip" => "gzip",
        _ => throw new ArgumentException($"'{extension}' is not an archive format 7-Zip can write.", nameof(extension)),
    };

    /// <summary>
    /// Extracts an archive with its folder structure intact. <c>-y</c> answers every
    /// overwrite prompt automatically — there is nothing interactive to answer it, so
    /// without this flag a name clash would hang the process instead of failing it.
    /// <c>-sccUTF-8</c> makes 7-Zip itself write its console output (a file name in an
    /// error message, for instance) as UTF-8 — its Windows build otherwise defaults to the
    /// system's legacy OEM code page there regardless of how the .NET side decodes it, so
    /// forcing this is what makes <see cref="ProcessRunner"/>'s own UTF-8 decoding correct
    /// rather than just consistently applied to the wrong encoding.
    /// </summary>
    public static IReadOnlyList<string> Extract(string archivePath, string outputDirectory) =>
        new[] { "x", archivePath, $"-o{outputDirectory}", "-y", "-sccUTF-8" };

    /// <summary>
    /// Creates a new archive of the given type from the given files.
    /// </summary>
    /// <remarks>
    /// <c>-t&lt;type&gt;</c> says what to write explicitly rather than leaving 7-Zip to guess
    /// from the output file's extension — the two usually agree, but guessing is exactly
    /// the kind of thing that should not be left implicit in a command a user cannot see.
    /// </remarks>
    public static IReadOnlyList<string> CreateArchive(string outputPath, IReadOnlyList<string> sourcePaths, string archiveType)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(outputPath);

        if (sourcePaths.Count == 0)
        {
            throw new ArgumentException("An archive needs at least one file to contain.", nameof(sourcePaths));
        }

        var arguments = new List<string> { "a", $"-t{archiveType}", outputPath };
        arguments.AddRange(sourcePaths);
        arguments.Add("-y");
        arguments.Add("-sccUTF-8");
        return arguments;
    }
}
