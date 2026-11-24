using System.Globalization;
using MediaSuite.Core.Jobs;
using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Engines;

/// <summary>
/// Builds command lines for the three tools behind the PDF module: QPDF for page-level
/// editing, Ghostscript for anything that touches how a page renders, and MuPDF for
/// rasterising and assembling pages. Pure, so every argument choice can be checked without
/// the tools installed.
/// </summary>
public static class PdfCommandBuilder
{
    /// <summary>Which external tool actually runs a given PDF operation.</summary>
    public static ExternalToolId ToolFor(string operationId) => operationId.ToLowerInvariant() switch
    {
        "pdf.merge" or "pdf.rotate" or "pdf.unlock" or "pdf.protect" or "pdf.flatten"
            or "pdf.organize" or "pdf.remove-pages" or "pdf.extract-pages" or "pdf.split" => ExternalToolId.QPdf,
        "pdf.crop" or "pdf.resize" or "pdf.compress" => ExternalToolId.Ghostscript,
        "pdf.to-jpg" or "pdf.extract-images" => ExternalToolId.MuPdf,
        "pdf.to-word" => ExternalToolId.LibreOffice,
        "image.jpg-to-pdf" => ExternalToolId.MuPdf,

        // image.heic-to-pdf and pdf.convert are handled by the engine, which picks a tool
        // per file rather than one tool for the whole operation.
        _ => throw new ArgumentException($"'{operationId}' has no single tool to ask for.", nameof(operationId)),
    };

    // --- QPDF ---------------------------------------------------------------------------

    /// <summary>Combines whole PDFs, in the order given, into one.</summary>
    public static IReadOnlyList<string> Merge(IReadOnlyList<string> inputPaths, string outputPath)
    {
        var arguments = new List<string> { "--empty", "--pages" };
        arguments.AddRange(inputPaths);
        arguments.Add("--");
        arguments.Add(outputPath);
        return arguments;
    }

    /// <summary>
    /// Rotates pages by a multiple of 90 degrees. A null range rotates the whole document.
    /// </summary>
    public static IReadOnlyList<string> Rotate(string inputPath, string outputPath, int degrees, string? pageRange)
    {
        var rotation = degrees.ToString(CultureInfo.InvariantCulture)
            + (pageRange is { Length: > 0 } ? $":{pageRange}" : string.Empty);

        // The flag comes before the files: qpdf's own convention is
        // "qpdf [options] infile [outfile]", and it is also what keeps the output path
        // last, which every other command in this file relies on too.
        return new[] { $"--rotate={rotation}", inputPath, outputPath };
    }

    /// <summary>Removes encryption from a PDF whose password is known.</summary>
    public static IReadOnlyList<string> Unlock(string inputPath, string outputPath, string password)
    {
        var arguments = new List<string> { "--decrypt" };

        if (password.Length > 0)
        {
            arguments.Add($"--password={password}");
        }

        arguments.Add(inputPath);
        arguments.Add(outputPath);
        return arguments;
    }

    /// <summary>
    /// Encrypts a PDF with 256-bit AES. The owner password defaults to the user password
    /// when none is given, since qpdf requires both.
    /// </summary>
    public static IReadOnlyList<string> Protect(
        string inputPath,
        string outputPath,
        string userPassword,
        string? ownerPassword,
        bool allowPrinting,
        bool allowCopying)
    {
        var effectiveOwnerPassword = string.IsNullOrEmpty(ownerPassword) ? userPassword : ownerPassword;

        var arguments = new List<string> { "--encrypt" };

        // qpdf's original "--encrypt user-password owner-password key-length" syntax takes
        // both passwords as bare positional values -- a password beginning with '-' (a real
        // password someone might genuinely choose) makes qpdf's own argument parser mistake
        // it for an unrecognized option instead of the password value, failing the whole job
        // with a confusing qpdf-internal error ("unrecognized argument ... encryption options
        // must be terminated with --") rather than ever applying the protection the user
        // asked for. qpdf 11.7.0 added --user-password=/--owner-password=/--bits= as
        // single-token alternatives specifically so any text can be used as a password;
        // using them here closes that gap, the same way Unlock just above already uses
        // --password= rather than a bare positional argument for the same reason.
        if (userPassword.Length > 0)
        {
            arguments.Add($"--user-password={userPassword}");
        }

        if (effectiveOwnerPassword.Length > 0)
        {
            arguments.Add($"--owner-password={effectiveOwnerPassword}");
        }

        arguments.Add("--bits=256");
        arguments.Add($"--print={(allowPrinting ? "full" : "none")}");
        arguments.Add($"--extract={(allowCopying ? "y" : "n")}");
        arguments.Add("--");
        arguments.Add(inputPath);
        arguments.Add(outputPath);

        return arguments;
    }

    /// <summary>Flattens form fields and annotations into the page content.</summary>
    public static IReadOnlyList<string> Flatten(string inputPath, string outputPath) =>
        new[] { "--flatten-annotations=all", inputPath, outputPath };

    /// <summary>
    /// Reorders, duplicates or deletes pages by giving qpdf the exact list of pages the
    /// output should contain, in order — the same mechanism covers organise, remove and
    /// extract, since all three are really just "here is the page list I want".
    /// </summary>
    public static IReadOnlyList<string> SelectPages(string inputPath, string outputPath, string pageList) =>
        new[] { inputPath, "--pages", ".", pageList, "--", outputPath };

    /// <summary>Asks qpdf how many pages a PDF has, needed to compute a "remove these" list.</summary>
    public static IReadOnlyList<string> CountPages(string inputPath) =>
        new[] { "--show-npages", inputPath };

    /// <summary>Splits a PDF into groups of <paramref name="pagesPerFile"/> pages each.</summary>
    public static IReadOnlyList<string> SplitEvery(string inputPath, string outputPattern, int pagesPerFile) =>
        new[] { $"--split-pages={pagesPerFile.ToString(CultureInfo.InvariantCulture)}", inputPath, outputPattern };

    // --- Ghostscript ----------------------------------------------------------------------

    /// <summary>
    /// Trims a fixed margin off every side of every page. Ghostscript reads the margin as
    /// how much to shrink the page by, not the size of the page that results, so the same
    /// number works regardless of the source page size.
    /// </summary>
    public static IReadOnlyList<string> Crop(string inputPath, string outputPath, double marginPoints)
    {
        var margin = marginPoints.ToString("0.##", CultureInfo.InvariantCulture);

        return new[]
        {
            "-sDEVICE=pdfwrite",
            "-dCompatibilityLevel=1.4",
            "-dNOPAUSE",
            "-dBATCH",
            "-dQUIET",
            "-o",
            outputPath,

            // -c takes the PostScript as its own argument, then -f switches back to file
            // mode for the actual input — gs treats everything between -c and -f as code.
            "-c",
            $"<</Margins [{margin} {margin}]>> setpagedevice",
            "-f",
            inputPath,
        };
    }

    /// <summary>Scales every page to fit a target paper size without changing its content.</summary>
    public static IReadOnlyList<string> Resize(string inputPath, string outputPath, double widthPoints, double heightPoints) => new[]
    {
        "-sDEVICE=pdfwrite",
        "-dCompatibilityLevel=1.4",
        "-dPDFFitPage",
        "-dFIXEDMEDIA",
        $"-dDEVICEWIDTHPOINTS={widthPoints.ToString("0.##", CultureInfo.InvariantCulture)}",
        $"-dDEVICEHEIGHTPOINTS={heightPoints.ToString("0.##", CultureInfo.InvariantCulture)}",
        "-dNOPAUSE",
        "-dBATCH",
        "-dQUIET",
        "-o",
        outputPath,
        inputPath,
    };

    /// <summary>
    /// Recompresses a PDF at one of Ghostscript's downsampling presets, from smallest
    /// (screen) to highest fidelity (prepress).
    /// </summary>
    public static IReadOnlyList<string> Compress(string inputPath, string outputPath, string pdfSettings) => new[]
    {
        "-sDEVICE=pdfwrite",
        "-dCompatibilityLevel=1.4",
        $"-dPDFSETTINGS=/{pdfSettings}",
        "-dNOPAUSE",
        "-dBATCH",
        "-dQUIET",
        "-o",
        outputPath,
        inputPath,
    };

    /// <summary>Preset name Ghostscript expects for a quality preset.</summary>
    public static string PdfSettingsFor(QualityPreset preset) => preset switch
    {
        QualityPreset.Quick => "screen",
        QualityPreset.Best => "prepress",
        _ => "ebook",
    };

    /// <summary>Page size in points for the paper sizes the resize tool offers.</summary>
    public static (double WidthPoints, double HeightPoints) PaperSize(string name) => name.ToLowerInvariant() switch
    {
        "letter" => (612, 792),
        "legal" => (612, 1008),
        "a3" => (841.89, 1190.55),
        "a5" => (419.53, 595.28),
        _ => (595.28, 841.89), // A4
    };

    // --- MuPDF --------------------------------------------------------------------------

    /// <summary>Renders every page of a PDF to a numbered image at the given resolution.</summary>
    public static IReadOnlyList<string> RenderPages(string inputPath, string outputPattern, int dpi) =>
        new[] { "draw", "-o", outputPattern, "-r", dpi.ToString(CultureInfo.InvariantCulture), inputPath };

    /// <summary>Combines images, one per page, into a single PDF.</summary>
    public static IReadOnlyList<string> ImagesToPdf(IReadOnlyList<string> imagePaths, string outputPath)
    {
        var arguments = new List<string> { "convert", "-o", outputPath };
        arguments.AddRange(imagePaths);
        return arguments;
    }

    /// <summary>Extracts every embedded image from a PDF into the process's working directory.</summary>
    public static IReadOnlyList<string> ExtractImages(string inputPath) => new[] { "extract", inputPath };

    /// <summary>Rewrites a PDF through qpdf unchanged — used when "converting" PDF to PDF.</summary>
    public static IReadOnlyList<string> PassThrough(string inputPath, string outputPath) =>
        new[] { inputPath, outputPath };

    /// <summary>DPI for rendering a PDF page to an image, from the quality preset.</summary>
    public static int DpiFor(QualityPreset preset) => preset switch
    {
        QualityPreset.Quick => 96,
        QualityPreset.Best => 300,
        _ => 150,
    };
}
