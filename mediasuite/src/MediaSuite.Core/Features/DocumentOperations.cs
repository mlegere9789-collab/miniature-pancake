namespace MediaSuite.Core.Features;

/// <summary>The document and ebook operations, and what each implies about output format.</summary>
public static class DocumentOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "document.convert",
        "ebook.convert",
        "pdf.to-epub",
        "ebook.epub-to-pdf",
        "document.docx-to-pdf",
    };

    /// <summary>
    /// Format the operation forces, or null when the user picks — "Document Converter"
    /// and "Ebook Converter" are the open-ended ones; the rest are named for one format.
    /// </summary>
    public static string? FixedFormatFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "pdf.to-epub" => "epub",
        "ebook.epub-to-pdf" or "document.docx-to-pdf" => "pdf",
        _ => null,
    };
}
