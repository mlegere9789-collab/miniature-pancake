using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds command lines for the three tools behind the document and ebook module: Pandoc
/// for the DOCX/ODT/RTF/HTML/Markdown/plain-text matrix, LibreOffice for anything touching
/// the legacy binary DOC format or producing a PDF, and Calibre for anything touching an
/// ebook format. Pure, so every choice can be checked without the tools installed.
/// </summary>
public static class DocumentCommandBuilder
{
    /// <summary>
    /// Which tool converts between two document extensions, for the Document Converter's
    /// open matrix (never a PDF endpoint — DOCX to PDF is its own fixed-format tool and
    /// always uses LibreOffice, the same way PDF to Word does in the PDF module).
    /// LibreOffice is Pandoc's fallback rather than its equal: Pandoc's DOCX reader only
    /// understands the modern OOXML format, and it has no DOC reader or writer at all, so
    /// old Word files have to go through LibreOffice's own DOC filter on either end.
    /// </summary>
    public static ExternalToolId ToolForDocumentPair(string fromExtension, string toExtension) =>
        IsLegacyDoc(fromExtension) || IsLegacyDoc(toExtension)
            ? ExternalToolId.LibreOffice
            : ExternalToolId.Pandoc;

    private static bool IsLegacyDoc(string extension) =>
        string.Equals(extension.TrimStart('.'), "doc", StringComparison.OrdinalIgnoreCase);

    /// <summary>Pandoc's own name for a document format, given its file extension.</summary>
    public static string PandocFormatFor(string extension) => extension.TrimStart('.').ToLowerInvariant() switch
    {
        "docx" => "docx",
        "odt" => "odt",
        "rtf" => "rtf",
        "html" or "htm" => "html",
        "md" => "markdown",

        // Plain text has no markup to speak of, and pandoc's markdown reader copes with
        // unmarked prose perfectly well — using it for .txt input keeps paragraph breaks
        // intact instead of running everything together the way the "plain" reader would.
        "txt" => "markdown",

        _ => throw new ArgumentException($"'{extension}' is not a document format Pandoc handles.", nameof(extension)),
    };

    /// <summary>
    /// Converts one document to another through Pandoc.
    /// </summary>
    /// <remarks>
    /// <c>--standalone</c> is always passed. It is what makes an HTML or plain-text target
    /// a complete document instead of a body fragment; for a container format like DOCX or
    /// ODT it changes nothing, so there is no reason to leave it off case by case.
    /// </remarks>
    public static IReadOnlyList<string> Convert(string inputPath, string outputPath, string fromExtension, string toExtension) =>
        new[]
        {
            "--standalone",
            "-f", PandocFormatFor(fromExtension),
            "-t", PandocFormatFor(toExtension),
            "-o", outputPath,
            inputPath,
        };

    /// <summary>
    /// Converts through LibreOffice's headless batch mode: DOC on either end, or any
    /// document going to PDF.
    /// </summary>
    /// <remarks>
    /// LibreOffice names the output by the input's own file name, not by an argument, and
    /// only accepts a destination folder — never a destination file name — so the engine
    /// has to move the result into place itself afterwards.
    /// </remarks>
    public static IReadOnlyList<string> ConvertViaLibreOffice(string inputPath, string outputDirectory, string toExtension) =>
        new[] { "--headless", "--convert-to", toExtension.TrimStart('.').ToLowerInvariant(), "--outdir", outputDirectory, inputPath };

    /// <summary>
    /// Converts an ebook, or a PDF to or from one, through Calibre. <c>ebook-convert</c>
    /// takes no format flag at all — it reads the target format from the output file's own
    /// extension, so getting that extension right is the entire command.
    /// </summary>
    public static IReadOnlyList<string> ConvertEbook(string inputPath, string outputPath) =>
        new[] { inputPath, outputPath };
}
