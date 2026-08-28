namespace MediaSuite.Core.Features;

/// <summary>The PDF operations, and what each implies about inputs and output format.</summary>
public static class PdfOperations
{
    public static IReadOnlySet<string> All { get; } = new HashSet<string>(StringComparer.OrdinalIgnoreCase)
    {
        "pdf.convert",
        "pdf.to-word",
        "pdf.to-jpg",
        "image.heic-to-pdf",
        "image.jpg-to-pdf",
        "pdf.compress",
        "pdf.merge",
        "pdf.split",
        "pdf.flatten",
        "pdf.resize",
        "pdf.unlock",
        "pdf.rotate",
        "pdf.protect",
        "pdf.crop",
        "pdf.organize",
        "pdf.extract-images",
        "pdf.remove-pages",
        "pdf.extract-pages",
    };

    /// <summary>
    /// Format the operation forces, or null when the user (or, for pdf.convert, the input
    /// file at run time) decides.
    /// </summary>
    public static string? FixedFormatFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "pdf.to-word" => "docx",
        "pdf.to-jpg" => "jpg",
        "image.heic-to-pdf" or "image.jpg-to-pdf" => "pdf",

        // pdf.convert and pdf.extract-images decide their format from the input at run
        // time — a PDF page rasterises to whatever image format was chosen, and an
        // extracted image keeps whatever format it was embedded as.
        "pdf.convert" or "pdf.extract-images" => null,

        var id when All.Contains(id) => "pdf",
        _ => null,
    };

    /// <summary>
    /// Operations that turn several PDFs (or several images) into one PDF, so the queue
    /// must not split them into a job per file.
    /// </summary>
    public static bool CombinesInputs(string operationId) => operationId.ToLowerInvariant() switch
    {
        "pdf.merge" or "image.heic-to-pdf" or "image.jpg-to-pdf" => true,
        _ => false,
    };

    /// <summary>Operations that can turn one input into more than one output file.</summary>
    public static bool ProducesManyOutputs(string operationId) => operationId.ToLowerInvariant() switch
    {
        "pdf.split" or "pdf.extract-images" or "pdf.to-jpg" or "pdf.convert" => true,
        _ => false,
    };
}
