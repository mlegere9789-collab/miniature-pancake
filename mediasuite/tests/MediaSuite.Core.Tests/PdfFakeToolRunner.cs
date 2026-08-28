using MediaSuite.Core.Tooling;

namespace MediaSuite.Core.Tests;

/// <summary>
/// Stands in for QPDF, Ghostscript, MuPDF, LibreOffice and ImageMagick, so the PDF engine
/// can be tested without any of them installed.
/// </summary>
/// <remarks>
/// Unlike <see cref="FakeProcessRunner"/>, this cannot assume "the last argument is the
/// output": that is true for QPDF's positional style, but Ghostscript and MuPDF take an
/// explicit <c>-o</c> flag, LibreOffice takes <c>--outdir</c>, and MuPDF's page-render and
/// image-extract commands each produce several files rather than one. Each tool gets its
/// own, faithful simulation instead of one generic rule.
/// </remarks>
public sealed class PdfFakeToolRunner : IProcessRunner
{
    public List<ProcessRequest> Requests { get; } = new();

    /// <summary>Pages the fake PDF is pretended to have, for split and render commands.</summary>
    public int PageCount { get; set; } = 3;

    /// <summary>Images the fake PDF is pretended to contain, for extract-images.</summary>
    public int ExtractedImageCount { get; set; } = 2;

    /// <summary>When set, the next call fails with this stderr instead of succeeding.</summary>
    public string? NextFailure { get; set; }

    public IReadOnlyList<ProcessRequest> RequestsFor(string executableFragment) =>
        Requests.Where(r => r.FileName.Contains(executableFragment, StringComparison.OrdinalIgnoreCase)).ToList();

    public Task<ProcessResult> RunAsync(ProcessRequest request, CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();

        lock (Requests)
        {
            Requests.Add(request);
        }

        if (NextFailure is { } failure)
        {
            NextFailure = null;
            return Task.FromResult(new ProcessResult(1, string.Empty, failure, TimeSpan.Zero));
        }

        var file = request.FileName;
        var args = request.Arguments;

        if (file.Contains("qpdf", StringComparison.OrdinalIgnoreCase))
        {
            return RunQPdf(args);
        }

        if (file.Contains("gswin64c", StringComparison.OrdinalIgnoreCase)
            || file.Contains("ghostscript", StringComparison.OrdinalIgnoreCase))
        {
            Write(AfterFlag(args, "-o"));
            return Ok();
        }

        if (file.Contains("mutool", StringComparison.OrdinalIgnoreCase))
        {
            return RunMuTool(args, request.WorkingDirectory);
        }

        if (file.Contains("soffice", StringComparison.OrdinalIgnoreCase))
        {
            var outdir = AfterFlag(args, "--outdir");
            var name = Path.GetFileNameWithoutExtension(args[^1]);
            Write(Path.Combine(outdir, name + ".docx"));
            return Ok();
        }

        // ImageMagick (the HEIC-to-JPEG pre-step) always ends its argument list with the
        // output path, the same convention ImageCommandBuilder is already tested against.
        Write(args[^1]);
        return Ok();
    }

    private Task<ProcessResult> RunQPdf(IReadOnlyList<string> args)
    {
        if (args.Contains("--show-npages"))
        {
            return Ok(PageCount.ToString(System.Globalization.CultureInfo.InvariantCulture));
        }

        var splitFlag = args.FirstOrDefault(a => a.StartsWith("--split-pages=", StringComparison.Ordinal));

        if (splitFlag is not null)
        {
            var pagesPerFile = int.Parse(splitFlag["--split-pages=".Length..], System.Globalization.CultureInfo.InvariantCulture);
            var pattern = args[^1];
            var parts = (int)Math.Ceiling(PageCount / (double)Math.Max(1, pagesPerFile));

            for (var i = 1; i <= parts; i++)
            {
                Write(pattern.Replace("%d", i.ToString(System.Globalization.CultureInfo.InvariantCulture), StringComparison.Ordinal));
            }

            return Ok();
        }

        // Every other QPDF command in PdfCommandBuilder ends with the output path.
        Write(args[^1]);
        return Ok();
    }

    private Task<ProcessResult> RunMuTool(IReadOnlyList<string> args, string? workingDirectory)
    {
        switch (args.Count > 0 ? args[0] : string.Empty)
        {
            case "draw":
                var pattern = AfterFlag(args, "-o");

                for (var page = 1; page <= PageCount; page++)
                {
                    Write(pattern.Replace("%d", page.ToString(System.Globalization.CultureInfo.InvariantCulture), StringComparison.Ordinal));
                }

                return Ok();

            case "convert":
                Write(AfterFlag(args, "-o"));
                return Ok();

            case "extract":
                var directory = workingDirectory ?? ".";

                for (var image = 1; image <= ExtractedImageCount; image++)
                {
                    Write(Path.Combine(directory, $"img{image:0000}.png"));
                }

                return Ok();

            default:
                return Ok();
        }
    }

    private static string AfterFlag(IReadOnlyList<string> args, string flag)
    {
        var index = args.ToList().IndexOf(flag);
        return index >= 0 && index + 1 < args.Count ? args[index + 1] : args[^1];
    }

    private static Task<ProcessResult> Ok(string stdout = "") =>
        Task.FromResult(new ProcessResult(0, stdout, string.Empty, TimeSpan.FromMilliseconds(1)));

    private static void Write(string path)
    {
        var directory = Path.GetDirectoryName(path);

        if (!string.IsNullOrEmpty(directory))
        {
            Directory.CreateDirectory(directory);
        }

        File.WriteAllText(path, "generated by PdfFakeToolRunner");
    }
}
